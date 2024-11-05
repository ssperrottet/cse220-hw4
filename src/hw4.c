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
void begin_game_logic(int conn_fd, GameState *state, int *board_width, int *board_height, const char *buffer, int player_num);
void init_game_logic(int conn_fd, GameState *state, int *board_width, int *board_height, int game_boards[2][MAX_SIZE][MAX_SIZE], const char *buffer, int player_num);
void rotate_90_clockwise(int shape[SHIP_SIZE][SHIP_SIZE], int rotatedShape[SHIP_SIZE][SHIP_SIZE]);
void print_board(int shape[MAX_SIZE][MAX_SIZE], int width, int height);

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

    for (int shape = 0; shape < NUM_SHAPES; shape++)
    {
        printf("Shape %d:\n", shape + 1);
        for (int rotation = 0; rotation < ROTATIONS; rotation++)
        {
            printf("Rotation %d degrees:\n", rotation * 90);
            printShape(ship_shapes[shape][rotation]);
        }
    }

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

    // Game state
    GameState state[2] = {STATE_BEGIN, STATE_BEGIN};

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

    // Main game loop
    while (1)
    {
        for (int i = 0; i < 2; i++)
        {
            char buffer[BUFFER_SIZE];
            memset(buffer, 0, BUFFER_SIZE);
            int valid_input = 0;

            while (!valid_input)
            {
                int nbytes = read_message(conn_fds[i], buffer, BUFFER_SIZE);
                if (nbytes <= 0)
                {
                    printf("[Server] Client on port %d disconnected.\n", ports[i]);
                    state[i] = STATE_DISCONNECTED;
                    close(conn_fds[i]);

                    // Check if both clients disconnected
                    if (state[0] == STATE_DISCONNECTED && state[1] == STATE_DISCONNECTED)
                    {
                        printf("[Server] Both clients disconnected. Shutting down server.\n");
                        close(listen_fds[0]);
                        close(listen_fds[1]);
                        return EXIT_SUCCESS;
                    }
                    break;
                }

                printf("[Server] Received from client on port %d: %s\n", ports[i], buffer);

                int player_num = (ports[i] == PORT1) ? 1 : 2;

                if (strcmp(buffer, "F") == 0)
                {
                    printf("[Server] Client on port %d has forfeited.\n", ports[i]);
                    send_response(conn_fds[i], "H 0");
                    send_response(conn_fds[(i + 1) % 2], "H 1");
                    state[i] = STATE_DISCONNECTED;
                    
                    state[(i + 1) % 2] = STATE_DISCONNECTED;
                    close(conn_fds[i]);
                    close(conn_fds[(i + 1) % 2]);
                    break;
                }

                // Handle different game states
                if (state[i] == STATE_BEGIN)
                {
                    begin_game_logic(conn_fds[i], &state[i], &board_width, &board_height, buffer, player_num);
                    valid_input = (state[i] == STATE_INIT);
                }
                else if (state[i] == STATE_INIT)
                {
                    init_game_logic(conn_fds[i], &state[i], &board_width, &board_height, game_boards, buffer, player_num);
                    valid_input = (state[i] == STATE_PLAYING);
                }
                else if (state[i] == STATE_PLAYING)
                {
                    // Placeholder for game logic

                    send_response(conn_fds[i], "A");
                    valid_input = 1;
                }
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

void begin_game_logic(int conn_fd, GameState *state, int *board_width, int *board_height, const char *buffer, int player_num)
{
    // Check if the message starts with 'B'
    if (buffer[0] == 'B')
    {
        if (player_num == 1)
        {
            // Player 1 sending board size and other parameters
            int arguments;
            int *size = convert_to_int_array(buffer + 1, &arguments); // Skip 'B'

            // Validate parameters: need at least 2 arguments and board dimensions should be at least 10
            if (arguments < 2 || size[0] < 10 || size[1] < 10)
            {
                // Invalid parameters provided by Player 1
                send_response(conn_fd, "E 200"); // Invalid parameters
                free(size);
                return;
            }

            // Set the board dimensions and acknowledge Player 1
            *board_width = size[0];
            *board_height = size[1];
            *state = STATE_INIT;
            printf("[Server] Board will be %d by %d.\n", *board_width, *board_height);
            send_response(conn_fd, "A"); // Acknowledgment for Player 1 ready
            free(size);
        }
        else
        {
            int arguments;
            int *size = convert_to_int_array(buffer + 1, &arguments);
            if (arguments > 0)
            {
                // Invalid parameters provided by Player 1
                send_response(conn_fd, "E 200"); // Invalid parameters
                free(size);
                return;
            }
            // Player 2 response, just acknowledge the start
            printf("[Server] Player 2 is ready. Starting game.\n");
            *state = STATE_INIT;
            send_response(conn_fd, "A"); // Acknowledgment for Player 2 ready
            free(size);
        }
    }
    else
    {
        // If the message doesn't start with 'B', handle invalid input
        printf("[Server] Invalid input from Player %d: %s\n", player_num, buffer);
        send_response(conn_fd, "E 100"); // Invalid command
    }
}

void init_game_logic(int conn_fd, GameState *state, int *board_width, int *board_height, int game_boards[2][MAX_SIZE][MAX_SIZE], const char *buffer, int player_num)
{
    if (buffer[0] == 'I')
    {

        int arguments;
        int *setup = convert_to_int_array(buffer + 1, &arguments);

        // Validate parameters: need at least 2 arguments and board dimensions should be at least 10
        if (arguments >= PIECE_COUNT * 4)
        {
            for (int i = 0; i < PIECE_COUNT; i++)
            {
                int type = setup[i * 4];
                int rotation = setup[i * 4 + 1];
                int col = setup[i * 4 + 2];
                int row = setup[i * 4 + 3];

                if (type < 1 || type > 7)
                {
                    printf("[Server] Shape out of range.");
                    send_response(conn_fd, "E 300");
                    free(setup);
                    return;
                }
                if (rotation < 1 || rotation > 4)
                {
                    printf("[Server] Rotation out of range.");
                    send_response(conn_fd, "E 301");
                    free(setup);
                    return;
                }
                if (row < 0 || row > *board_height || col < 0 || col > *board_width)
                {
                    printf("[Server] Position out of game board.");
                    send_response(conn_fd, "E 302");
                    free(setup);
                    return;
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
                int(*board)[MAX_SIZE] = game_boards[player_num - 1];

                for (int j = 0; j < SHIP_SIZE; j++)
                {
                    for (int k = 0; k < SHIP_SIZE; k++)
                    {

                        int board_row = row - row_pos + j;
                        int board_col = col - col_pos + k;
                        if (board_row < 0 || board_row > *board_height || board_col < 0 || board_col > *board_width)
                        {
                            for (int i = 0; i < MAX_SIZE; i++)
                            {
                                for (int j = 0; j < MAX_SIZE; j++)
                                {
                                    game_boards[player_num - 1][i][j] = 0;
                                }
                            }
                            printf("[Server] Doesn't fit.");
                            send_response(conn_fd, "E 302");
                            free(setup);
                            return;
                        }
                        if (shape[j][k])
                        {
                            if (board[board_row][board_col] == 0)
                            {
                                board[board_row][board_col] = shape[j][k];
                            }
                            else
                            {
                                for (int i = 0; i < MAX_SIZE; i++)
                                {
                                    for (int j = 0; j < MAX_SIZE; j++)
                                    {
                                        game_boards[player_num - 1][i][j] = 0;
                                    }
                                }
                                printf("[Server] Overlap");
                                send_response(conn_fd, "E 303");
                                free(setup);
                                return;
                            }
                        }
                    }
                }
                print_board(board, *board_width, *board_height);
            }
        }
        else
        {
            printf("[Server] Invalid input from Player %d: %s\n", player_num, buffer);
            send_response(conn_fd, "E 201");
            free(setup);
            return;
        }

        *state = STATE_PLAYING;
        printf("[Server] Player %d has initialized their pieces.\n", player_num);
        send_response(conn_fd, "A");
        free(setup);
    }
    else
    {
        printf("[Server] Invalid input from Player %d: %s\n", player_num, buffer);
        send_response(conn_fd, "E 101");
    }
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
