#ifndef BATTLESHIP_H
#define BATTLESHIP_H

#include <pthread.h>
#include <stdbool.h>
#include <ctype.h>
#include "../common/protocol.h"

#define BOARD_SIZE    8
#define TOTAL_SHIPS   4  // SUBMARINO(1) + FRAGATA(2) + DESTROYER(1)
#define MAX_NAME_LEN  32
#define MAX_CLIENTS   2

// Tipos de navio
typedef enum { SUBMARINE = 1, FRAGATA = 2, DESTROYER = 3 } ShipType;
typedef enum { HORIZONTAL, VERTICAL } Orientation;

// Estrutura de coordenada
typedef struct { int x, y; } Coord;

// Tabuleiro protegido por mutex
typedef struct {
    int grid[BOARD_SIZE][BOARD_SIZE];  // 0=vazio, >0=navio intacto, <0=navio atingido
    pthread_mutex_t lock;
} Board;

// Representação de uma embarcação
typedef struct {
    ShipType   type;              // tipo (tamanho)
    int        size;              // cópia de (int)type
    int        hits;              // quantos acertos já sofreu
    Coord      coords[BOARD_SIZE];// posições ocupadas
    int        coord_count;       // quantas posições (== size)
    bool       placed;            // se já foi posicionado
} Ship;

// Estado de cada jogador
typedef struct {
    int sockfd;
    char name[MAX_NAME_LEN];
    int player_id;         // 1 ou 2
    bool joined;           // se fez JOIN
    Ship     ships[TOTAL_SHIPS];
    int      ship_count;        // quantos embarcações já registrou (até 4)
    Board board;
    bool ready;        // Se já está pronto
    bool active_turn;  // Seu turno está ativo
} Player;

// Estado global do jogo
typedef struct {
    Player players[MAX_CLIENTS];
    int count;
    pthread_mutex_t mutex;     // protege players e cond_ready
    pthread_cond_t cond_ready; // sinaliza quando ambos deram READY
    bool game_over;
    bool game_started;         // controla se o jogo já começou
} Game;

// Seta o necessário para inicar um jogo
void init_game(Game *game);
// Adiciona um jogador ao servidor
bool add_player(Game *game, int sockfd);
// Lida com os comandos que o player manda para o servidor
void *handle_client(void *arg);
// Envia uma mensagem para um jogador em especifíco
void send_to_player(Player *p, const char *msg);
// Envia uma mensagem para ambos os jogadores
void broadcast(const Game *game, const char *msg);
void process_command(Game *game, Player *p, const char *cmd);
// Verifica se pode posicionar uma embarcação na coordenas solicitadas
bool can_place(Player *p, ShipType type, Coord c, Orientation o);
// Adiciona a embarcação nas coordenadas solicitadas
bool place_ship(Player *p, ShipType type, Coord c, Orientation o);
// Lida com as regras de um tiro por um jogador
void handle_fire(Game *game, Player *p, Coord c);
// Verifica as condições para um jogador vencer
bool check_winner(Game *game, Player **winner, Player **loser);
// Faz a conversão de uma string para o tipo de embarcação(enum ShyType)
ShipType parse_ship_type(const char *s);
// Procura por um player pelo id do socket
Player* find_player_by_socket(Game *g, int sockfd);

#endif // BATTLESHIP_H