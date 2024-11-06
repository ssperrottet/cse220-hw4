#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <asm-generic/socket.h>

#define PORT1 2201
#define PORT2 2202
#define BUFFER_SIZE 1024
#define MAX_SIZE 24
#define PIECE_COUNT 5

#define NUM_SHAPES 7
#define ROTATIONS 4
#define SHIP_SIZE 4

typedef enum
{
    STATE_BEGIN,
    STATE_INIT,
    STATE_PLAYING,
    STATE_DISCONNECTED
} GameState;

int *convert_to_int_array(const char *input_str, int *size);
void send_response(int conn_fd, const char *response);
int read_message(int conn_fd, char *buffer, int buffer_size);
void rotate_90_clockwise(int shape[SHIP_SIZE][SHIP_SIZE], int rotatedShape[SHIP_SIZE][SHIP_SIZE]);
void print_board(int shape[MAX_SIZE][MAX_SIZE], int width, int height);
void clear_board(int shape[MAX_SIZE][MAX_SIZE], int width, int height);

int ship_shapes[NUM_SHAPES][ROTATIONS][SHIP_SIZE][SHIP_SIZE] = {
    {{// Shape 1: 0° rotation
      {1, 1, 0, 0},
      {1, 1, 0, 0},
      {0, 0, 0, 0},
      {0, 0, 0, 0}}},
    {{// Shape 2: 0° rotation
      {1, 0, 0, 0},
      {1, 0, 0, 0},
      {1, 0, 0, 0},
      {1, 0, 0, 0}}},
    {{// Shape 3: 0° rotation
      {0, 1, 1, 0},
      {1, 1, 0, 0},
      {0, 0, 0, 0},
      {0, 0, 0, 0}}},
    {{// Shape 4: 0° rotation
      {1, 0, 0, 0},
      {1, 0, 0, 0},
      {1, 1, 0, 0},
      {0, 0, 0, 0}}},
    {{// Shape 5: 0° rotation
      {1, 1, 0, 0},
      {0, 1, 1, 0},
      {0, 0, 0, 0},
      {0, 0, 0, 0}}},
    {{// Shape 6: 0° rotation
      {0, 1, 0, 0},
      {0, 1, 0, 0},
      {1, 1, 0, 0},
      {0, 0, 0, 0}}},
    {{// Shape 7: 0° rotation
      {1, 1, 1, 0},
      {0, 1, 0, 0},
      {0, 0, 0, 0},
      {0, 0, 0, 0}}}};

void precomputeRotations()
{
    for (int shape = 0; shape < NUM_SHAPES; shape++)
    {
        // Generate the 90°, 180°, and 270° rotations
        for (int rotation = 1; rotation < ROTATIONS; rotation++)
        {
            rotate_90_clockwise(ship_shapes[shape][rotation - 1], ship_shapes[shape][rotation]);
        }
    }
}

void printShape(int shape[SHIP_SIZE][SHIP_SIZE])
{
    for (int i = 0; i < SHIP_SIZE; i++)
    {
        for (int j = 0; j < SHIP_SIZE; j++)
        {
            printf("%d ", shape[i][j]);
        }
        printf("\n");
    }
    printf("\n");
}

int main()
{
    precomputeRotations();

    int listen_fds[2], conn_fds[2];
    struct sockaddr_in addresses[2];
    int ports[2] = {PORT1, PORT2};
    int opt = 1;
    int addrlen = sizeof(struct sockaddr_in);
    char buffer[BUFFER_SIZE] = {0};

    // Initialize boards and guesses
    int board_width = 0;
    int board_height = 0;

    int game_boards[2][MAX_SIZE][MAX_SIZE] = {0};
    int ships_remaining[2] = {PIECE_COUNT, PIECE_COUNT};

    // Set up the sockets for both players
    for (int i = 0; i < 2; i++)
    {
        if ((listen_fds[i] = socket(AF_INET, SOCK_STREAM, 0)) == 0)
        {
            perror("socket failed");
            exit(EXIT_FAILURE);
        }

        if (setsockopt(listen_fds[i], SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
        {
            perror("setsockopt failed");
            close(listen_fds[i]);
            exit(EXIT_FAILURE);
        }

        addresses[i].sin_family = AF_INET;
        addresses[i].sin_addr.s_addr = INADDR_ANY;
        addresses[i].sin_port = htons(ports[i]);

        if (bind(listen_fds[i], (struct sockaddr *)&addresses[i], sizeof(addresses[i])) < 0)
        {
            perror("bind failed");
            close(listen_fds[i]);
            exit(EXIT_FAILURE);
        }

        if (listen(listen_fds[i], 1) < 0)
        {
            perror("listen failed");
            close(listen_fds[i]);
            exit(EXIT_FAILURE);
        }

        printf("[Server] Listening on port %d\n", ports[i]);
    }

    // Accept incoming connections
    for (int i = 0; i < 2; i++)
    {
        printf("[Server] Waiting for client on port %d...\n", ports[i]);
        if ((conn_fds[i] = accept(listen_fds[i], (struct sockaddr *)&addresses[i], (socklen_t *)&addrlen)) < 0)
        {
            perror("accept failed");
            close(listen_fds[i]);
            exit(EXIT_FAILURE);
        }

        printf("[Server] Client connected on port %d\n", ports[i]);
    }

    int game_active = 1;
    GameState state = STATE_BEGIN;

    while (game_active) // Main game loop
    {
        for (int player = 1, player_id = 0; player <= 2; player++, player_id++)
        {       
            int pending_move = 1;
            print_board(game_boards[player_id], board_width, board_height);
            while (pending_move && game_active)
            {
                memset(buffer, 0, BUFFER_SIZE);
                int nbytes = read_message(conn_fds[player - 1], buffer, BUFFER_SIZE);
                if (nbytes <= 0)
                {
                    printf("[Server] Could not read from port %d.\n", ports[player_id]);
                    continue;
                }

                // Display command
                printf("[Server] Received from client on port %d: %s\n", ports[player_id], buffer);
                char command = buffer[0];
                int arg_count;
                int *arguments = convert_to_int_array(buffer + 1, &arg_count); // Read arguments to the command

                if (command == 'F') // Forfeit
                {
                    printf("[Server] Client on port %d has forfeited.\n", ports[player_id]);
                    send_response(conn_fds[player_id], "H 0");  // player who forfeits
                    send_response(conn_fds[player % 2], "H 1"); // notify winner
                    state = STATE_DISCONNECTED;
                    pending_move = 0;
                }
                else if (state == STATE_BEGIN)
                {
                    if (command != 'B')
                    { // If the message doesn't start with 'B', handle invalid input

                        printf("[Server] Invalid input from Player %d: %s\n", player, buffer);
                        send_response(conn_fds[player_id], "E 100"); // Invalid command
                        continue;
                    }

                    if ((player == 1 && arg_count != 2) || (player == 2 && arg_count != 0))
                    {
                        printf("[Server] Invalid arguments from Player %d: %s\n", player, buffer);
                        send_response(conn_fds[player_id], "E 200"); // Invalid parameters
                        continue;
                    }

                    if (player == 1) // PLAYER 1 BEGIN
                    {
                        if (arguments[0] < 10 || arguments[1] < 10)
                        {
                            send_response(conn_fds[player_id], "E 200");
                            continue;
                        }
                        board_width = arguments[0];
                        board_height = arguments[1];

                        printf("[Server] Board will be %d by %d.\n", board_width, board_height);
                    }
                    else // PLAYER 2 BEGIN
                    {
                        printf("[Server] Starting game.\n", board_width, board_height);
                        state = STATE_INIT;
                    }

                    send_response(conn_fds[player_id], "A"); // Acknowledgment for Player 1 ready
                    pending_move = 0;
                }
                else if (state == STATE_INIT)
                {
                    if (command != 'I')
                    { // If the message doesn't start with 'I', handle invalid input

                        printf("[Server] Invalid input from Player %d: %s\n", player, buffer);
                        send_response(conn_fds[player_id], "E 101"); // Invalid command
                        continue;
                    }
                    if (arg_count != PIECE_COUNT * 4)
                    {
                        printf("[Server] Invalid input from Player %d: %s\n", player, buffer);
                        send_response(conn_fds[player_id], "E 201"); // Invalid arguments
                        continue;
                    }

                    for (int i = 0; i < PIECE_COUNT; i++)
                    {
                        int type = arguments[i * 4];
                        int rotation = arguments[i * 4 + 1];
                        int col = arguments[i * 4 + 2];
                        int row = arguments[i * 4 + 3];

                        if (type < 1 || type > 7)
                        {
                            printf("[Server] Shape out of range.");
                            send_response(conn_fds[player_id], "E 300");
                            continue;
                            ;
                        }
                        if (rotation < 1 || rotation > 4)
                        {
                            printf("[Server] Rotation out of range.");
                            send_response(conn_fds[player_id], "E 301");
                            continue;
                        }
                        if (row < 0 || row > board_height || col < 0 || col > board_width)
                        {
                            printf("[Server] Position out of game board.");
                            send_response(conn_fds[player_id], "E 302");
                            continue;
                        }

                        int(*shape)[SHIP_SIZE] = ship_shapes[type - 1][rotation - 1];

                        int row_pos, col_pos;
                        for (int i = 0, found = 0; i < SHIP_SIZE && !found; i++)
                        {
                            for (int j = 0; j < SHIP_SIZE && !found; j++)
                            {
                                if (shape[j][i] == 1)
                                {
                                    row_pos = j;
                                    col_pos = i;
                                    found = 1;
                                }
                            }
                        }
                        int(*board)[MAX_SIZE] = game_boards[player_id];

                        for (int j = 0; j < SHIP_SIZE; j++)
                        {
                            for (int k = 0; k < SHIP_SIZE; k++)
                            {

                                int board_row = row - row_pos + j;
                                int board_col = col - col_pos + k;
                                if (board_row < 0 || board_row > board_height || board_col < 0 || board_col > board_width)
                                {
                                    for (int i = 0; i < MAX_SIZE; i++)
                                    {
                                        for (int j = 0; j < MAX_SIZE; j++)
                                        {
                                            game_boards[player_id][i][j] = 0;
                                        }
                                    }
                                    printf("[Server] Doesn't fit.");
                                    send_response(conn_fds[player_id], "E 302");
                                    continue;
                                }
                                if (shape[j][k])
                                {
                                    if (board[board_row][board_col] == 0)
                                    {
                                        board[board_row][board_col] = i + 1;
                                    }
                                    else
                                    {
                                        for (int i = 0; i < MAX_SIZE; i++)
                                        {
                                            for (int j = 0; j < MAX_SIZE; j++)
                                            {
                                                game_boards[player_id][i][j] = 0;
                                            }
                                        }
                                        printf("[Server] Overlap");
                                        send_response(conn_fds[player_id], "E 303");
                                        continue;
                                    }
                                }
                            }
                        }
                        // print_board(board, board_width, board_height);
                    }
                    send_response(conn_fds[player_id], "A");
                    pending_move = 0;
                    if (player == 2)
                    {
                        state = STATE_PLAYING;
                    }
                }
                else if (state == STATE_PLAYING)
                {
                    if (command == 'Q' && arg_count == 0)
                    {
                        buffer[0] = 'G';
                        buffer[1] = ' ';
                        buffer[2] = '0' + ships_remaining[player_id % 2];
                        buffer[3] = ' ';
                        int index = 4;
                        for (int i = 0; i < board_height; i++)
                        {
                            for (int j = 0; j < board_width; j++)
                            {
                                if (game_boards[player % 2][i][j] == 'M' || game_boards[player % 2][i][j] < 0)
                                {
                                    buffer[index] = (game_boards[player_id % 2][i][j] < 0) ? 'H' : 'M';
                                    buffer[index + 1] = ' ';
                                    buffer[index + 2] = '0' + j;
                                    buffer[index + 3] = ' ';
                                    buffer[index + 4] = '0' + i;
                                    buffer[index + 5] = ' ';
                                    index += 6;
                                }
                            }
                            
                        }
                        buffer[index - 1] = '\0';
                        send_response(conn_fds[player_id], buffer);
                    }
                    else if (command == 'S' && arg_count == 2)
                    {
                        int row = arguments[1];
                        int col = arguments[0];
                        char response[] = "R 0 M";
                        if (col < 0 || col > board_width || col < 0 || col > board_height)
                        {
                            send_response(conn_fds[player_id], "E 400");
                            continue;
                        }
                        if (game_boards[player % 2][row][col] < 0 || game_boards[player % 2][row][col] == 'M')
                        {
                            send_response(conn_fds[player_id], "E 401");
                            continue;
                        }
                        else if (game_boards[player % 2][row][col] == 0)
                        {

                            response[2] = '0' + ships_remaining[player % 2];
                            game_boards[player % 2][row][col] = 'M';
                        }
                        else
                        {
                            int ship_id = game_boards[player % 2][row][col];
                            game_boards[player % 2][row][col] *= -1;
                            int ship_sunk = 1;
                            for (int i = 0; i < board_height && ship_sunk; i++)
                            {
                                for (int j = 0; j < board_width && ship_sunk; j++)
                                {
                                    if (game_boards[player % 2][i][j] == ship_id)
                                    {
                                        ship_sunk = 0;
                                    }
                                }
                            }
                            if (ship_sunk)
                            {
                                ships_remaining[player % 2]--;
                            }

                            response[2] = '0' + ships_remaining[player % 2];
                            response[4] = 'H';
                        }
                        send_response(conn_fds[player_id], response);
                        pending_move = 0;
                    }
                    else
                    {
                        send_response(conn_fds[player_id], "E 102");
                        continue;
                    }
                }
                free(arguments);
            }

            // Check if both clients are disconnected
            if (state == STATE_DISCONNECTED)
            {
                printf("[Server] Both clients disconnected. Shutting down server.\n");
                close(listen_fds[0]);
                close(listen_fds[1]);
                return EXIT_SUCCESS;
            }
        }
    }

    // Close listening sockets
    for (int i = 0; i < 2; i++)
    {
        close(listen_fds[i]);
    }

    printf("[Server] Shutting down.\n");
    return EXIT_SUCCESS;
}

void send_response(int conn_fd, const char *response)
{
    send(conn_fd, response, strlen(response), 0);
}

int read_message(int conn_fd, char *buffer, int buffer_size)
{
    return read(conn_fd, buffer, buffer_size);
}

int *convert_to_int_array(const char *input_str, int *size)
{
    int *int_array = malloc(MAX_SIZE * sizeof(int));
    int count = 0;
    const char *ptr = input_str;

    while (*ptr && !isdigit(*ptr))
    {
        ptr++;
    }

    while (*ptr)
    {
        int num = 0;
        while (*ptr && isdigit(*ptr))
        {
            num = num * 10 + (*ptr - '0');
            ptr++;
        }
        int_array[count++] = num;
        while (*ptr && !isdigit(*ptr))
        {
            ptr++;
        }
    }
    *size = count;
    return int_array;
}

void rotate_90_clockwise(int shape[SHIP_SIZE][SHIP_SIZE], int rotatedShape[SHIP_SIZE][SHIP_SIZE])
{
    for (int i = 0; i < SHIP_SIZE; i++)
    {
        for (int j = 0; j < SHIP_SIZE; j++)
        {
            rotatedShape[j][3 - 1 - i] = shape[i][j];
        }
    }
}

void print_board(int shape[MAX_SIZE][MAX_SIZE], int width, int height)
{
    for (int i = 0; i < width; i++)
    {
        for (int j = 0; j < height; j++)
        {
            printf("%d ", shape[i][j]);
        }
        printf("\n");
    }
    printf("\n");
}

void clear_board(int shape[MAX_SIZE][MAX_SIZE], int width, int height)
{
    for (int i = 0; i < width; i++)
    {
        for (int j = 0; j < height; j++)
        {
            shape[i][j] = 0;
        }
    }
}