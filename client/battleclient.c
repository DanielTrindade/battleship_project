#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <ctype.h>

#include "battleship.h"
#include "../common/protocol.h"

#define SERVER_PORT 8080
#define SERVER_IP "127.0.0.1"

void print_instructions() {
    printf("\n=== BATALHA NAVAL ===\n");
    printf("Comandos disponíveis:\n");
    printf("  JOIN <seu_nome>                 - Entrar no jogo\n");
    printf("  POS <tipo> <x> <y> <H/V>       - Posicionar navio\n");
    printf("  READY                          - Confirmar posicionamento\n");
    printf("  FIRE <x> <y>                   - Atacar posição\n");
    printf("\nTipos de navios:\n");
    printf("  SUBMARINO (tamanho 1) - 1 unidade\n");
    printf("  FRAGATA (tamanho 2)   - 2 unidades\n");
    printf("  DESTROYER (tamanho 3) - 1 unidade\n");
    printf("\nCoordenadas: x e y de 1 a 8\n");
    printf("Orientação: H (horizontal) ou V (vertical)\n");
    printf("========================\n\n");
}

void print_board_guide() {
    printf("\n=== REFERÊNCIA DO TABULEIRO ===\n");
    printf("  1 2 3 4 5 6 7 8\n");
    for (int i = 1; i <= 8; i++) {
        printf("%d ", i);
        for (int j = 1; j <= 8; j++) {
            printf("· ");
        }
        printf("\n");
    }
    printf("===============================\n\n");
}

void print_separator() {
    printf("----------------------------------------\n");
}

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in serv = {0};
    serv.sin_family = AF_INET;
    serv.sin_port   = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serv.sin_addr);

    printf("Conectando ao servidor...\n");
    if (connect(sock, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        perror("Erro ao conectar");
        close(sock);
        return 1;
    }

    printf("Conectado ao servidor!\n");
    print_instructions();
    print_board_guide();
    
    printf("> ");
    fflush(stdout);

    fd_set fds;
    char buf[MAX_MSG];
    bool game_ended = false;

    while (!game_ended) {
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        FD_SET(STDIN_FILENO, &fds);

        if (select(sock + 1, &fds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }

        // Mensagem do servidor
        if (FD_ISSET(sock, &fds)) {
            ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
            if (n <= 0) {
                printf("Conexão com servidor perdida.\n");
                break;
            }
            buf[n] = '\0';
            
            // Remove \n no final se houver
            if (buf[n-1] == '\n') {
                buf[n-1] = '\0';
            }
            
            // Limpa a linha atual do prompt
            printf("\r");
            fflush(stdout);
            
            // Exibe a mensagem do servidor
            printf("%s\n", buf);

            // Verifica se é fim de jogo
            if (strstr(buf, "VENCEU") || strstr(buf, "PERDEU") || 
                strstr(buf, "FINALIZADO") || strstr(buf, "END")) {
                printf("\nPressione Enter para sair...\n");
                getchar();
                game_ended = true;
                continue;
            }
            
            // Mostra prompt após mensagens importantes
            if (strstr(buf, "SUA VEZ") || strstr(buf, "Digite") || 
                strstr(buf, "COMANDO") || strstr(buf, "ERRO") ||
                strstr(buf, "AGUARDE") || strstr(buf, "PRONTO") ||
                strstr(buf, "POSICIONADO") || strstr(buf, "READY")) {
                printf("\n> ");
                fflush(stdout);
            } else {
                // Para outras mensagens, apenas mostra o prompt simples
                printf("> ");
                fflush(stdout);
            }
        }

        // Input do usuário
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            if (!fgets(buf, sizeof(buf), stdin)) {
                break;
            }
            
            // Remove \n do final
            buf[strcspn(buf, "\n")] = '\0';
            
            // Se string vazia, ignora
            if (strlen(buf) == 0) {
                continue;
            }
            
            // Adiciona \n para o servidor
            strcat(buf, "\n");
            
            if (send(sock, buf, strlen(buf), 0) < 0) {
                perror("Erro ao enviar comando");
                break;
            }
        }
    }

    close(sock);
    printf("Desconectado do servidor.\n");
    return 0;
}