#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <termios.h>


#define PC1_CAN_ID 0x101
#define PC2_CAN_ID 0x102
#define BROADCAST_ID 0x200

enum Command {
    CMD_UP = 0,
    CMD_DOWN = 1,
    CMD_LEFT = 2,
    CMD_RIGHT = 3,
    CMD_GAME_END = 1
};

void set_terminal_mode(int enable) {
    static struct termios oldt, newt;
    if (enable) {
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }
}


int wait_for_can(int s, int expected_cmd) {
    struct can_frame frame;
    while (1) {
        int nbytes = read(s, &frame, sizeof(frame));
        if (nbytes > 0 && frame.can_dlc > 0) {
            if (frame.data[0] == expected_cmd) {
                return 1;
            }
            // Print for debug
            if (frame.data[0] == CMD_GAME_END) {
                printf("Game ended (received GAME_END).\n");
            }
        }
    }
    return 0;
}

int main() {
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) { perror("socket"); return 1; }

    struct ifreq ifr;
    strcpy(ifr.ifr_name, "can0");
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) { perror("ioctl"); return 1; }

    struct sockaddr_can addr;
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }

    set_terminal_mode(1);
    while (1) {
        printf("\n--- Waiting for any key to start/init game (press 'q' to quit) ---\n");
        int c = getchar();
        if (c == 'q') break;

        // Game loop: listen for keypresses and send commands, until GAME_END
        int game_running = 1;
        while (game_running) {
            // Non-blocking check for CAN GAME_END
            fd_set readfds;
            struct timeval tv = {0, 10000}; // 10ms
            FD_ZERO(&readfds);
            FD_SET(s, &readfds);
            int r = select(s+1, &readfds, NULL, NULL, &tv);
            if (r > 0 && FD_ISSET(s, &readfds)) {
                struct can_frame rx;
                int nbytes = read(s, &rx, sizeof(rx));
                if (nbytes > 0 && rx.can_dlc > 0 && rx.data[0] == CMD_GAME_END) {
                    printf("Game ended!\n");
                    game_running = 0;
                    break;
                }
            }

            // Non-blocking keypress
            int cmd = -1, can_id = 0;
            int key_ready = 0;
            struct timeval tv_key = {0, 10000};
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(STDIN_FILENO, &rfds);
            if (select(STDIN_FILENO+1, &rfds, NULL, NULL, &tv_key) > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
                int c = getchar();
                // Arrow keys for PC1
                if (c == 27) {
                    if (getchar() == 91) {
                        switch(getchar()) {
                            case 'A': cmd = CMD_UP;    can_id = PC1_CAN_ID; printf("PC1 UP\n"); break;
                            case 'B': cmd = CMD_DOWN;  can_id = PC1_CAN_ID; printf("PC1 DOWN\n"); break;
                            case 'C': cmd = CMD_RIGHT; can_id = PC1_CAN_ID; printf("PC1 RIGHT\n"); break;
                            case 'D': cmd = CMD_LEFT;  can_id = PC1_CAN_ID; printf("PC1 LEFT\n"); break;
                        }
                    }
                } else {
                    switch (c) {
                        case 'w': cmd = CMD_UP;    can_id = PC2_CAN_ID; printf("PC2 UP\n"); break;
                        case 's': cmd = CMD_DOWN;  can_id = PC2_CAN_ID; printf("PC2 DOWN\n"); break;
                        case 'a': cmd = CMD_LEFT;  can_id = PC2_CAN_ID; printf("PC2 LEFT\n"); break;
                        case 'd': cmd = CMD_RIGHT; can_id = PC2_CAN_ID; printf("PC2 RIGHT\n"); break;
                        case 'q': game_running = 0; break;
                    }
                }
                key_ready = 1;
            }
            if (cmd != -1 && can_id != 0 && key_ready) {
                struct can_frame tx = {0};
                tx.can_id = can_id;
                tx.can_dlc = 1;
                tx.data[0] = cmd;
                write(s, &tx, sizeof(tx));
            }
        }
        // Wait 3 seconds before allowing new game
        printf("Waiting 3 seconds before allowing new game...\n");
        sleep(3);
    }
    set_terminal_mode(0);
    close(s);
    return 0;
}
