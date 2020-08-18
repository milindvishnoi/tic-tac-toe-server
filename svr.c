#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_LEN 2000

struct client {
    int sd;
    struct client *next;
    struct sockaddr_in *addr;
    int bytes_in_buf;
    char *bufend;
    char buf[MAX_LEN];
};

// To store the players
struct game {
    struct client *x_player;
    struct client *o_player;
    char turn;
    char board[9];
    struct client *whos_player;
};

struct client *head = NULL;

int main(int argc, char **argv) {
    extern void add_node(int client_socket, struct sockaddr_in *addr, struct game *game);
    extern void showboard(int fd, struct game *game);
    extern void reset_board(struct game *game);
    extern void annouce_msg(struct client *client, char *msg);
    extern char *extractline(char *p, int size);
    extern void annouce_winner(char winner);
    extern int game_is_over(struct game *game);
    extern void disconnnect(struct client *client, struct game *game);
    extern void handle_read(struct client *node, struct game *game);

    int c, port = 3000, status = 0;
    int listen_socket;
    struct sockaddr_in svr;

    struct game *game = (struct game*)malloc(sizeof(struct game));
    if (game == NULL) {
        fprintf(stderr, "fatal error: out of memory");
    }
    game->x_player = NULL;
    game->o_player = NULL;
    game->turn = 'x';
    game->whos_player = (game->x_player);
    reset_board(game);

    while ((c = getopt(argc, argv, "p:")) != EOF) {
        switch (c) {
        case 'p':
            port = atoi(optarg);
            break;
        default:
            status = 1;
        }
    }
    if (status || optind < argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        exit(1);
    }

    // Setting up socket, bind and listen
    if ((listen_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }
    memset(&svr, '\0', sizeof svr);
    svr.sin_family = AF_INET;
    svr.sin_addr.s_addr = INADDR_ANY;
    svr.sin_port = htons(port);
    if (bind(listen_socket, (struct sockaddr *)&svr, sizeof svr) < 0) {
        perror("bind");
        exit(1);
    }
    if (listen(listen_socket, 5)) {
        perror("listen");
        exit(1);
    }

    fd_set readfds;
    int max_sd, activity, client_socket, sd;
    struct client *curr;
    while (1) {
        // clear the socket set
        FD_ZERO(&readfds);

        // add listen socket to the set
        FD_SET(listen_socket, &readfds);
        max_sd = listen_socket;
        // setting sds to readfd and getting highest sd
        for (curr = head; curr != NULL; curr = curr->next) {
            int sd = curr->sd;
            if (sd > 0)
                FD_SET(sd, &readfds);
            if (sd > max_sd)
                max_sd = sd;
        }
        // Wait for activity
        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0)
            perror("select");

        // Checking if we get a connect request in listen socket
        if (FD_ISSET(listen_socket, &readfds)) {
            // Accept connection
            struct sockaddr_in client;
            unsigned int size = sizeof(client);
            client.sin_family = PF_INET;
            if ((client_socket = accept(listen_socket, (struct sockaddr *)&client, &size)) < 0) {
                perror("accept");
            } else {
                printf("new connection from %s\n", inet_ntoa(client.sin_addr));
                //show new connection greeting message
                showboard(client_socket, game);
                add_node(client_socket, &client, game);
                if (client_socket > max_sd) {
                    FD_SET(client_socket, &readfds);
                    max_sd = client_socket;
                }
            }
        } else {
            for (curr = head; curr != NULL; curr = curr->next) {
                sd = curr->sd;
                if (FD_ISSET(sd, &readfds)) {
                    // Reading
                    // Move the data to buf
                    if (curr->bytes_in_buf && curr->bufend)
                        memmove(curr->buf, curr->bufend, curr->bytes_in_buf);

                    int new_bytes;
                    // When we get nothing
                    if ((new_bytes = read(sd, curr->buf + curr->bytes_in_buf, sizeof(curr->buf) - curr->bytes_in_buf - 1)) < 0) {
                        perror("read");
                        disconnnect(curr, game);
                        break;
                    }
                    else if (new_bytes == 0) {
                        // When we get 0 byte in read
                        disconnnect(curr, game);
                        break;
                    } else {
                        // To get the updated bytes
                        curr->bytes_in_buf += new_bytes;
                        while ((curr->bufend = extractline(curr->buf, curr->bytes_in_buf))) {
                            handle_read(curr, game);
                            curr->bytes_in_buf -= (curr->bufend - curr->buf);
                            memmove(curr->buf, curr->bufend, curr->bytes_in_buf);
                        }
                    }
                    // Checking if the buf is full
                    if (strlen(curr->buf) >= MAX_LEN - 1) {
                        handle_read(curr, game);
                        strcpy(curr->buf, "");
                        curr->bytes_in_buf = 0;
                        curr->bufend = NULL;
                    }
                }
            }
        }
    }
    return 0;
}

void message_client(struct client *client, char *msg) {
    // Does not contain \r\n so need to put from msg
    int len = strlen(msg);
    if (write(client->sd, msg, len) != len)
        perror("write");
}

void add_node(int client_socket, struct sockaddr_in *addr, struct game *game) {
    struct client **ptr = &head;
    struct client *newItem;
    newItem = (struct client*)malloc(sizeof(struct client));
    if (newItem == NULL) {
        fprintf(stderr, "fatal error: out of memory");
    }
    newItem->sd = client_socket;
    newItem->next = NULL;
    newItem->addr = addr;
    newItem->bufend = NULL;

    for (; ptr && *ptr && (*ptr)->next; ptr = &(*ptr)->next);
    struct client *new = &(*(*ptr));
    *ptr = newItem;
    (*ptr)->next = new;

    // Check if x and o players are defined
    char temp[100];
    if (game->x_player == NULL) {
        game->x_player = newItem;
        printf("client from %s is now x\n", inet_ntoa(addr->sin_addr));
        if (write(client_socket, "You now get to play! You are now x.\r\n", 38) != 38)
            perror("write");
        if (game->o_player != NULL) {
            strcpy(temp, inet_ntoa(game->x_player->addr->sin_addr));
            strcat(temp, " is now playing 'x'.\r\n");
            temp[strlen(temp)] = '\0';
            message_client(game->o_player, temp);
        }
    }
    else if (game->o_player == NULL) {
        game->o_player = newItem;
        printf("client from %s is now o\n", inet_ntoa(addr->sin_addr));
        if (write(client_socket, "You now get to play! You are now o.\r\n", 38) != 38)
            perror("write");
        if (game->x_player != NULL) {
            strcpy(temp, inet_ntoa(game->o_player->addr->sin_addr));
            strcat(temp, " is now playing 'o'.\r\n");
            temp[strlen(temp)] = '\0';
            message_client(game->x_player, temp);
        }
    }
}

void showboard(int fd, struct game *game) {
    char buf[100], *bufp, *boardp;
    int col, row;

    for (bufp = buf, col = 0, boardp = game->board; col < 3; col++) {
        for (row = 0; row < 3; row++, bufp += 4)
            sprintf(bufp, " %c |", *boardp++);
        bufp -= 2;  // kill last " |"
        strcpy(bufp, "\r\n---+---+---\r\n");
        bufp = strchr(bufp, '\0');
    }
    if (write(fd, buf, bufp - buf) != bufp-buf)
        perror("write");
    // To display turn for different kinds of clients
    char temp[100] = {'\0'};
    if ((game->x_player && game->x_player->sd == fd && game->turn == 'x') || (game->o_player && game->o_player->sd == fd && game->turn == 'o'))
        strcpy(temp, "It's your turn\r\n");
    else if (game->turn == 'x')
        strcpy(temp, "It is x's turn\r\n");
    else if (game->turn == 'o')
        strcpy(temp, "It is o's turn\r\n");
    if (temp[0] != '\0')
        if (write(fd, temp, strlen(temp)) != strlen(temp))
            perror("write");
}

void reset_board(struct game *game) {
    for (int i = 0; i < 9; i++)
        game->board[i] = '1' + i;
}

char *extractline(char *p, int size)
/* returns pointer to string after, or NULL if there isn't an entire
 * line here.  If non-NULL, original p is now a valid string. */
{
    int nl;
    for (nl = 0; nl < size && p[nl] != '\r' && p[nl] != '\n'; nl++)
        ;
    if (nl == size)
        return(NULL);

    /*
     * There are three cases: either this is a lone \r, a lone \n, or a CRLF.
     */
    if (p[nl] == '\r' && nl + 1 < size && p[nl+1] == '\n') {
        /* CRLF */
        p[nl] = '\0';
        return(p + nl + 2);
    }
    else {
        /* lone \n or \r */
        p[nl] = '\0';
        return(p + nl + 1);
    }
}

// Announces msg to everyone expect the client supplied
void annouce_msg(struct client *client, char *msg) {
    // go through all clients
    struct client *curr;
    for (curr = head; curr != NULL; curr = curr->next) {
        if (client != curr) {
            message_client(curr, msg);
            message_client(curr, "\r\n");
        }
    }
    fflush(stdout);
}

int game_is_over(struct game *game)  /* returns winner, or ' ' for draw, or 0 for not over */
{
    int i, c;
    extern int allthree(int start, int offset, struct game *game);
    extern int isfull(struct game *game);

    for (i = 0; i < 3; i++)
        if ((c = allthree(i, 3, game)) || (c = allthree(i * 3, 1, game)))
            return(c);
    if ((c = allthree(0, 4, game)) || (c = allthree(2, 2, game)))
        return(c);
    if (isfull(game))
        return(' ');
    return(0);
}

int allthree(int start, int offset, struct game *game)
{
    if (game->board[start] > '9' && game->board[start] == game->board[start + offset]
        && game->board[start] == game->board[start + offset * 2])
        return(game->board[start]);
    return(0);
}

int isfull(struct game *game)
{
    int i;
    for (i = 0; i < 9; i++)
        if (game->board[i] < 'a')
            return(0);
    return(1);
}

void play_move(struct client *client, struct game *game) {
    int num = atoi(client->buf);
    game->board[num - 1] = game->turn;
    char temp[100] = {'\0'};
    if (game->turn == 'x') {
        strcat(temp, "x makes move ");
        strcat(temp, client->buf);
        game->turn = 'o';
    } else {
        strcat(temp, "o makes move ");
        strcat(temp, client->buf);
        game->turn = 'x';
    }
    strcat(temp, "\r\n");
    struct client *node = head;
    for (; node; node=node->next) {
        message_client(node, temp);
        showboard(node->sd, game);
    }
}

// Check it's the players turn
int check_player_turn(struct client *node, struct game *game) {
    if (game->x_player && game->x_player->sd == node->sd && game->turn == 'x')
        return 1;
    if (game->o_player && game->o_player->sd == node->sd && game->turn == 'o')
        return 1;
    return 0;
}

int nan_check(struct client *node) { 
    // NaN (Not a number) then 1 else 0
    if (atoi(node->buf) > 0)
        return 0;
    return 1;
}

int valid_move(struct client *node, struct game *game) {
    // Check if correct player is playing the turn
    // Returns 0 if buf > 9 or buf < 1 or board[buf-1] is full
    // 1 if valid move place
    int move_place = atoi(node->buf) - 1;
    if (game->board[move_place] == (node->buf[0]))
        return 1;
    return 0;
}

// Checks if the client is a player
// Return 1 if player else 0
int check_player(struct client *client, struct game *game) {
    if ((client == game->x_player) || (client == game->o_player))
        return 1;
    return 0;
}

// If a player disconnects then we use this function to assign a new player
void change_player(struct client *client, struct game *game) {
    struct client *curr = head;
    char temp[100] = { '\0' };
    if (game->x_player == client) {
        for (; curr; curr = curr->next)
            if (curr != game->o_player && curr != game->x_player) {
                printf("client from %s is now x\n", inet_ntoa(curr->addr->sin_addr));
                message_client(curr, "You now get to play!  You are now x.\r\n");
                game->x_player = curr;
                if (game->o_player != NULL) {
                    strcpy(temp, inet_ntoa(game->x_player->addr->sin_addr));
                    strcat(temp, " is now playing 'o'.\r\n");
                    temp[strlen(temp)] = '\0';
                    message_client(game->o_player, temp);
                }
                break;
            }
    } else if (game->o_player == client) {
        for (; curr; curr = curr->next)
            if (curr != game->o_player && curr != game->x_player) {
                printf("client from %s is now o\n", inet_ntoa(curr->addr->sin_addr));
                message_client(curr, "You now get to play!  You are now o.\r\n");
                game->o_player = curr;
                if (game->x_player != NULL) {
                    strcpy(temp, inet_ntoa(game->o_player->addr->sin_addr));
                    strcat(temp, " is now playing 'o'.\r\n");
                    temp[strlen(temp)] = '\0';
                    message_client(game->x_player, temp);
                }
                break;
            }
    }
    // To specify that the player to NULL if it is still client
    if (game->o_player == client)
        game->o_player = NULL;
    else if (game->x_player == client)
        game->x_player = NULL;
}

void disconnnect(struct client *client, struct game *game) {
    struct client **curr_client;
    for (curr_client = &head; *curr_client && *curr_client != client; curr_client = &(*curr_client)->next)
	;
    close(client->sd);
    if (*curr_client) {
        *curr_client = client->next;
        printf("disconnecting client %s\n", inet_ntoa(client->addr->sin_addr));
        // Need to check if this client was a player or not
        // check_player_change(client)
        if (check_player(client, game))
            change_player(client, game);
        free(client);
    } else
	    fprintf(stderr, "can't delete client as does not exist %s\n", inet_ntoa(client->addr->sin_addr));
}

// Switches the players x and y
void switch_players(struct game *game) {
    struct client *o_player = game->o_player;
    game->o_player = game->x_player;
    game->x_player = o_player;
    game->turn = 'x';
    message_client(game->o_player, "You are o.\r\n");
    message_client(game->x_player, "You are x.\r\n");
}

void handle_read(struct client *node, struct game *game) {
    // Check if disconnected/empty message
    if (strlen(node->buf) <= 0) {
        disconnnect(node, game);
    } else if (strlen(node->buf) == 1) {
        // Play move
        if (nan_check(node)) {
            printf("chat message: %c\n", node->buf[0]);
            annouce_msg(node, node->buf);
        }
        else if (check_player(node, game)) {
            if (check_player_turn(node, game)) {
                if (valid_move(node, game)) {
                    // Valid Move
                    printf("%s (%c) makes move %c\n", inet_ntoa(node->addr->sin_addr), game->turn, node->buf[0]);
                    play_move(node, game);
                } else {
                    // Place Taken
                    message_client(node, "That space is taken\r\n");
                    printf("%s tries to make move %c, but that space is taken\n", inet_ntoa(node->addr->sin_addr), node->buf[0]);
                }
            } else {
                // Not this player's move
                message_client(node, "It is not your turn\r\n");
                printf("%s tries to make move %c, but it's not their turn\n", inet_ntoa(node->addr->sin_addr), node->buf[0]);
            }
        } else {
            // Not a player
            message_client(node, "You can't make moves; you can only watch the game\r\n");
            printf("%s tries to make move %c, but they aren't playing\n", inet_ntoa(node->addr->sin_addr), node->buf[0]);
        }
        // Check winner
        char winner = '\0';
        if ((winner = game_is_over(game)) != 0) {
            // game is over
            char temp[100] = {'\0'};
            printf("Game Over!\n");
            if (winner == ' ') {
                strcat(temp, "It is a draw.\r\n");
                printf("It is a draw.\n");
            }
            if (winner == 'x') {
                printf("x wins.\n");
                strcat(temp, "x wins.\r\n");
            }
            if (winner == 'o') {
                printf("o wins.\n");
                strcat(temp, "o wins.\r\n");
            }
            temp[strlen(temp)] = '\0';
            struct client *curr = head;
            // To not show the turn line in showboard
            game->turn = ' ';
            for (; curr; curr = curr->next) {
                message_client(curr, "Game Over!\r\n");
                showboard(curr->sd, game);
                message_client(curr, temp);
                message_client(curr, "Let's play again!\r\n");
            }
            switch_players(game);
            reset_board(game);
            // Hard set game turn to x
            game->turn = 'x';
            for (curr = head; curr; curr = curr->next)
                showboard(curr->sd, game);
        }
    } else if (strlen(node->buf) > 1) {
        // this is message
        printf("chat message: %s\n", node->buf);
        annouce_msg(node, node->buf);
    }
}