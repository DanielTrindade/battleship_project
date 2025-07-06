#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "battleship.h"
#include "../common/protocol.h"

#define SERVER_PORT 8080
#define SERVER_IP "127.0.0.1"

Game game;           // estado global do jogo
int listenfd = -1;   // socket do servidor

ShipType parse_ship_type(const char *s) {
    if (strcmp(s, "SUBMARINO")  == 0) return SUBMARINE;
    if (strcmp(s, "FRAGATA")    == 0) return FRAGATA;
    if (strcmp(s, "DESTROYER")  == 0) return DESTROYER;
    return 0;
}

void init_game(Game *g) {
    pthread_mutex_init(&g->mutex, NULL);
    pthread_cond_init (&g->cond_ready, NULL);
    g->count        = 0;
    g->game_over    = false;
    g->game_started = false;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        Player *p = &g->players[i];
        p->sockfd       = -1;
        memset(p->name, 0, MAX_NAME_LEN);
        p->joined       = false;
        p->ready        = false;
        p->active_turn  = false;
        pthread_mutex_init(&p->board.lock, NULL);
        memset(p->board.grid, 0, sizeof(p->board.grid));

        // inicializa lista de ships
        p->ship_count = 0;
        for (int s = 0; s < TOTAL_SHIPS; s++) {
            p->ships[s].placed = false;
            p->ships[s].hits   = 0;
        }
    }
}

void send_to_player(Player *p, const char *msg) {
    if (p->sockfd != -1) {
        send(p->sockfd, msg, strlen(msg), 0);
    }
}

void broadcast(const Game *g, const char *msg) {
    for (int i = 0; i < g->count; i++) {
        if (g->players[i].sockfd != -1) {
            send(g->players[i].sockfd, msg, strlen(msg), 0);
        }
    }
}

Player* find_player_by_socket(Game *g, int sockfd) {
    for (int i = 0; i < g->count; i++) {
        if (g->players[i].sockfd == sockfd) {
            return &g->players[i];
        }
    }
    return NULL;
}

bool add_player(Game *g, int sockfd) {
    pthread_mutex_lock(&g->mutex);
    if (g->count >= MAX_CLIENTS) {
        pthread_mutex_unlock(&g->mutex);
        return false;
    }
    g->players[g->count].sockfd = sockfd;
    g->players[g->count].player_id = g->count + 1; // Player 1 ou 2
    g->count++;
    pthread_mutex_unlock(&g->mutex);
    return true;
}

bool can_place(Player *p, ShipType type, Coord c, Orientation o) {
    int size = (int)type;
    for (int i = 0; i < size; i++) {
        int x = c.x + (o == VERTICAL   ? i : 0);
        int y = c.y + (o == HORIZONTAL ? i : 0);
        if (x < 0 || x >= BOARD_SIZE ||
            y < 0 || y >= BOARD_SIZE) return false;
        if (p->board.grid[x][y] != 0)  return false;
    }
    return true;
}

bool place_ship(Player *p, ShipType type, Coord c, Orientation o) {
    int size = (int)type;
    if (!can_place(p, type, c, o)) return false;

    // bloqueia o tabuleiro
    pthread_mutex_lock(&p->board.lock);
    // marca no grid
    for (int i = 0; i < size; i++) {
        int x = c.x + (o == VERTICAL   ? i : 0);
        int y = c.y + (o == HORIZONTAL ? i : 0);
        p->board.grid[x][y] = type;
    }
    pthread_mutex_unlock(&p->board.lock);

    // registra no array de ships
    Ship *ship = &p->ships[p->ship_count++];
    ship->type        = type;
    ship->size        = size;
    ship->hits        = 0;
    ship->coord_count = size;
    ship->placed      = true;
    for (int i = 0; i < size; i++) {
        ship->coords[i] = (Coord){ c.x + (o == VERTICAL   ? i : 0),
                                   c.y + (o == HORIZONTAL ? i : 0) };
    }
    return true;
}

static Ship *get_ship_at_coord(Player *p, Coord c) {
    for (int i = 0; i < p->ship_count; i++) {
        Ship *s = &p->ships[i];
        if (!s->placed) continue;
        for (int j = 0; j < s->coord_count; j++) {
            if (s->coords[j].x == c.x && s->coords[j].y == c.y) {
                return s;
            }
        }
    }
    return NULL;
}

static int count_ships_of_type(Player *p, ShipType type) {
    int cnt = 0;
    for (int i = 0; i < p->ship_count; i++) {
        if (p->ships[i].type == type) cnt++;
    }
    return cnt;
}

void handle_fire(Game *g, Player *p, Coord c) {
    Player *opp = (p == &g->players[0])
                  ? &g->players[1]
                  : &g->players[0];
    int result = 0;  // 0 = ÁGUA, 1 = ACERTO, 2 = AFUNDOU

    // Converte para exibição (1..8)
    char display_coords[32];
    snprintf(display_coords, sizeof(display_coords), "%d %d",
             c.x + 1, c.y + 1);

    // Bloqueia o tabuleiro do oponente
    pthread_mutex_lock(&opp->board.lock);

    // Verifica limite e conteúdo
    if (c.x < 0 || c.x >= BOARD_SIZE ||
        c.y < 0 || c.y >= BOARD_SIZE ||
        opp->board.grid[c.x][c.y] <= 0)
    {
        result = 0;  // ÁGUA
    } else {
        // Marca o acerto
        int ship_type = opp->board.grid[c.x][c.y];
        opp->board.grid[c.x][c.y] = -ship_type;
        result = 1;  // ACERTO por padrão

        // Identifica e atualiza o Ship atingido
        Ship *hitShip = get_ship_at_coord(opp, c);
        if (hitShip) {
            hitShip->hits++;
            if (hitShip->hits >= hitShip->size) {
                result = 2;  // AFUNDOU
            }
        }
    }

    pthread_mutex_unlock(&opp->board.lock);

    // Texto do resultado
    const char *result_text = (result == 0) ? CMD_MISS
                             : (result == 1) ? CMD_HIT
                                             : CMD_SUNK;

    // Broadcast do ataque
    char msg[MAX_MSG];
    snprintf(msg, sizeof(msg),
             "=== PLAYER %d (%s) ATACOU %s: %s ===\n",
             p->player_id, p->name, display_coords, result_text);
    broadcast(g, msg);

    // Verifica vencedor
    Player *winner = NULL, *loser = NULL;
    if (check_winner(g, &winner, &loser)) {
        snprintf(msg, sizeof(msg),
                 "=== PARABÉNS %s (PLAYER %d)! VOCÊ VENCEU! ===\n",
                 winner->name, winner->player_id);
        send_to_player(winner, msg);

        snprintf(msg, sizeof(msg),
                 "=== %s (PLAYER %d) PERDEU! ===\n",
                 loser->name, loser->player_id);
        send_to_player(loser, msg);

        broadcast(g, "=== JOGO FINALIZADO ===\n");
        g->game_over = true;
        return;
    }

    // Alterna turnos
    p->active_turn   = false;
    opp->active_turn = true;

    // Próximo turno
    snprintf(msg, sizeof(msg),
             "\n--- TURNO DO PLAYER %d (%s) ---\n",
             opp->player_id, opp->name);
    broadcast(g, msg);
    send_to_player(opp, "*** SUA VEZ! Digite FIRE <x> <y> para atacar ***\n");
    send_to_player(p,   "*** AGUARDE O TURNO DO ADVERSÁRIO ***\n");
}

bool check_winner(Game *g, Player **winner, Player **loser) {
    if (!g->game_started) return false;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        bool has_ships = false;
        pthread_mutex_lock(&g->players[i].board.lock);
        for (int x = 0; x < BOARD_SIZE && !has_ships; x++) {
            for (int y = 0; y < BOARD_SIZE; y++) {
                if (g->players[i].board.grid[x][y] > 0) {
                    has_ships = true;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&g->players[i].board.lock);
        if (!has_ships) {
            *loser  = &g->players[i];
            *winner = &g->players[1 - i];
            return true;
        }
    }
    return false;
}

void process_command(Game *g, Player *p, const char *cmd) {
    if (g->game_over) return;

    // Copia cmd para uma string mutável e trim
    char clean_cmd[MAX_MSG];
    strncpy(clean_cmd, cmd, MAX_MSG-1);
    clean_cmd[MAX_MSG-1] = '\0';
    // Remove whitespace no fim
    char *end = clean_cmd + strlen(clean_cmd) - 1;
    while (end >= clean_cmd && isspace((unsigned char)*end)) {
        *end = '\0'; end--;
    }

    printf("[DEBUG] Player %d - Comando: '%s'\n", p->player_id, clean_cmd);

    // JOIN <nome>
    if (strncmp(clean_cmd, CMD_JOIN, strlen(CMD_JOIN)) == 0 &&
        isspace((unsigned char)clean_cmd[strlen(CMD_JOIN)]))
    {
        if (p->joined) {
            send_to_player(p, "ERRO: Você já está fez JOIN!\n");
            return;
        }
        char name[MAX_NAME_LEN];
        if (sscanf(clean_cmd + strlen(CMD_JOIN) + 1, "%31s", name) != 1) {
            send_to_player(p, "ERRO: Formato inválido! Use: JOIN <seu_nome>\n");
            return;
        }
        strncpy(p->name, name, MAX_NAME_LEN);
        p->joined = true;
        char msg[MAX_MSG];
        snprintf(msg, sizeof(msg),
                 "=== BEM-VINDO, %s! VOCÊ É O PLAYER %d ===\n",
                 p->name, p->player_id);
        send_to_player(p, msg);

        if (g->count == 1) {
            send_to_player(p, "*** AGUARDANDO OUTRO JOGADOR... ***\n");
        } else if (g->count == MAX_CLIENTS) {
            bool both = g->players[0].joined && g->players[1].joined;
            if (both) {
                broadcast(g, "\n=== AMBOS JOGADORES CONECTADOS ===\n");
                broadcast(g, "=== FASE DE POSICIONAMENTO INICIADA ===\n");
                broadcast(g, "*** POSICIONE SEUS NAVIOS: POS <tipo> <x> <y> <H/V> ***\n");
            }
        }
        return;
    }

    // verificar se deu JOIN
    if (!p->joined) {
        send_to_player(p, "ERRO: Faça JOIN <seu_nome> primeiro!\n");
        return;
    }

    // READY
    if (strcmp(clean_cmd, CMD_READY) == 0) {
        if (p->ship_count != TOTAL_SHIPS) {
            char msg[MAX_MSG];
            snprintf(msg, sizeof(msg),
                     "ERRO: Posicione todos os navios primeiro! (%d/%d)\n",
                     p->ship_count, TOTAL_SHIPS);
            send_to_player(p, msg);
            return;
        }
        if (p->ready) {
            send_to_player(p, "ERRO: Você já está pronto!\n");
            return;
        }
        p->ready = true;
        char msg[MAX_MSG];
        snprintf(msg, sizeof(msg),
                 "*** PLAYER %d (%s) ESTÁ PRONTO! ***\n",
                 p->player_id, p->name);
        broadcast(g, msg);

        // sinaliza condição de ambos prontos
        pthread_mutex_lock(&g->mutex);
        bool both = g->players[0].ready && g->players[1].ready;
        pthread_mutex_unlock(&g->mutex);
        if (both) {
            broadcast(g, "\n=== AMBOS JOGADORES PRONTOS ===\n");
            broadcast(g, "=== INICIANDO BATALHA NAVAL ===\n");
            g->game_started = true;
            g->players[0].active_turn = true;
            g->players[1].active_turn = false;

            snprintf(msg, sizeof(msg),
                     "\n--- TURNO DO PLAYER 1 (%s) ---\n",
                     g->players[0].name);
            broadcast(g, msg);
            send_to_player(&g->players[0],
                           "*** SUA VEZ! Digite FIRE <x> <y> ***\n");
        }
        return;
    }

    // POS <tipo> <x> <y> <H/V>
    if (strncmp(clean_cmd, CMD_POS, strlen(CMD_POS)) == 0) {
        if (g->game_started) {
            send_to_player(p, "ERRO: Jogo já iniciado!\n");
            return;
        }
        if (p->ready) {
            send_to_player(p, "ERRO: Você já está pronto!\n");
            return;
        }
        char type_str[16];
        int rx, ry;
        char ori;
        if (sscanf(clean_cmd + strlen(CMD_POS) + 1,
                   "%15s %d %d %c",
                   type_str, &rx, &ry, &ori) == 4)
        {
            if (rx < 1 || rx > BOARD_SIZE ||
                ry < 1 || ry > BOARD_SIZE)
            {
                send_to_player(p,
                    "ERRO: Coordenadas devem ser 1 a 8!\n");
                return;
            }
            ShipType type = parse_ship_type(type_str);
            if (type == 0) {
                send_to_player(p,
                    "ERRO: Tipo inválido! Use SUBMARINO, FRAGATA ou DESTROYER\n");
                return;
            }
            // limite de cada tipo
            int used = count_ships_of_type(p, type);
            int limit = (type == FRAGATA) ? 2 : 1;
            if (used >= limit) {
                send_to_player(p,
                    "ERRO: Limite deste tipo atingido!\n");
                return;
            }
            Coord c = { rx-1, ry-1 };
            Orientation o = (ori=='H'||ori=='h') ? HORIZONTAL : VERTICAL;
            if (place_ship(p, type, c, o)) {
                char msg[MAX_MSG];
                snprintf(msg, sizeof(msg),
                         "*** %s em %d,%d %c! (%d/%d navios) ***\n",
                         type_str, rx, ry, ori,
                         p->ship_count, TOTAL_SHIPS);
                send_to_player(p, msg);
                if (p->ship_count == TOTAL_SHIPS) {
                    send_to_player(p,
                        "*** TODOS POSICIONADOS! Digite READY ***\n");
                }
            } else {
                send_to_player(p,
                    "ERRO: Posição inválida ou ocupada!\n");
            }
        } else {
            send_to_player(p,
                "ERRO: Use: POS <tipo> <x> <y> <H/V>\n");
        }
        return;
    }

    // FIRE <x> <y>
    if (strncmp(clean_cmd, CMD_FIRE, strlen(CMD_FIRE)) == 0) {
        if (!g->game_started) {
            send_to_player(p, "ERRO: Jogo não iniciado!\n");
            return;
        }
        if (!p->active_turn) {
            Player *opp = (p == &g->players[0])
                          ? &g->players[1]
                          : &g->players[0];
            char msg[MAX_MSG];
            snprintf(msg, sizeof(msg),
                     "ERRO: Aguarde PLAYER %d (%s)\n",
                     opp->player_id, opp->name);
            send_to_player(p, msg);
            return;
        }
        int rx, ry;
        if (sscanf(clean_cmd + strlen(CMD_FIRE) + 1,
                   "%d %d", &rx, &ry) == 2)
        {
            if (rx < 1 || rx > BOARD_SIZE ||
                ry < 1 || ry > BOARD_SIZE)
            {
                send_to_player(p,
                    "ERRO: Coordenadas 1 a 8!\n");
                return;
            }
            handle_fire(g, p, (Coord){rx-1, ry-1});
        } else {
            send_to_player(p,
                "ERRO: Use: FIRE <x> <y>\n");
        }
        return;
    }

    // Comando inválido
    send_to_player(p,
        "COMANDO INVÁLIDO! JOIN, POS, READY ou FIRE\n");
}

void *handle_client(void *arg) {
    Player *p = arg;
    char buf[MAX_MSG];

    printf("[DEBUG] Cliente conectado (socket %d)\n", p->sockfd);

    while (!game.game_over) {
        int bytes = recv(p->sockfd, buf, MAX_MSG-1, 0);
        if (bytes <= 0) {
            printf("[DEBUG] Cliente desconectado (socket %d)\n", p->sockfd);
            break;
        }
        buf[bytes] = '\0';
        buf[strcspn(buf, "\r\n")] = '\0';
        if (strlen(buf) > 0) {
            process_command(&game, p, buf);
        }
    }

    close(p->sockfd);
    p->sockfd = -1;

    // decrementa contador e, se zerar, encerra servidor
    pthread_mutex_lock(&game.mutex);
    game.count--;
    if (game.count <= 0) {
        printf("[SERVER] Ambos jogadores desconectados, encerrando servidor.\n");
        game.game_over = true;
    }
    pthread_mutex_unlock(&game.mutex);

    return NULL;
}

int main() {
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(SERVER_PORT)
    };
    if (bind(listenfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind"); exit(1);
    }
    if (listen(listenfd, 5) == -1) {
        perror("listen"); exit(1);
    }

    init_game(&game);
    printf("[SERVER] Servidor Batalha Naval iniciado na porta %d\n", SERVER_PORT);
    printf("[SERVER] Aguardando jogadores...\n");

    // aceita exatamente MAX_CLIENTS jogadores
    while (game.count < MAX_CLIENTS) {
        int conn = accept(listenfd, NULL, NULL);
        if (conn == -1) {
            perror("accept");
            continue;
        }
        if (!add_player(&game, conn)) {
            send(conn, "ERRO: Jogo já está cheio!\n", 26, 0);
            close(conn);
            continue;
        }
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client,
                           &game.players[game.count-1]) != 0)
        {
            perror("pthread_create");
            close(conn);
            pthread_mutex_lock(&game.mutex);
            game.count--;
            pthread_mutex_unlock(&game.mutex);
            continue;
        }
        pthread_detach(tid);
    }

    // mantém servidor vivo até game_over = true
    while (!game.game_over) {
        sleep(1);
    }

    close(listenfd);
    printf("[SERVER] Servidor finalizado.\n");
    return 0;
}