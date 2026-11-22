#include <stdio.h>
int main(void) {
    char board[9];
    int player;
    int move;
    int turn;
    int won;
    board[0]='1'; board[1]='2'; board[2]='3';
    board[3]='4'; board[4]='5'; board[5]='6';
    board[6]='7'; board[7]='8'; board[8]='9';
    player=0;
    for (turn=0; turn<9; turn++) {
        printf("\n %c | %c | %c\n---+---+---\n %c | %c | %c\n---+---+---\n %c | %c | %c\n",
            board[0],board[1],board[2],board[3],board[4],board[5],board[6],board[7],board[8]);
        if (player == 0) printf("Player X, choose 1-9: ");
        else printf("Player O, choose 1-9: ");
        if (scanf("%d", &move) != 1 || move < 1 || move > 9 || board[move-1]=='X' || board[move-1]=='O') {
            puts("Invalid move.");
            turn=turn-1;
        } else {
            if (player == 0) board[move-1]='X';
            else board[move-1]='O';
            won=0;
            if (board[0]==board[1] && board[1]==board[2]) won=1;
            if (board[3]==board[4] && board[4]==board[5]) won=1;
            if (board[6]==board[7] && board[7]==board[8]) won=1;
            if (board[0]==board[3] && board[3]==board[6]) won=1;
            if (board[1]==board[4] && board[4]==board[7]) won=1;
            if (board[2]==board[5] && board[5]==board[8]) won=1;
            if (board[0]==board[4] && board[4]==board[8]) won=1;
            if (board[2]==board[4] && board[4]==board[6]) won=1;
            if (won) {
                printf("\n %c | %c | %c\n---+---+---\n %c | %c | %c\n---+---+---\n %c | %c | %c\n",
                    board[0],board[1],board[2],board[3],board[4],board[5],board[6],board[7],board[8]);
                if (player == 0) puts("X wins!"); else puts("O wins!");
                return 0;
            }
            if (player == 0) player=1; else player=0;
        }
    }
    printf("\n %c | %c | %c\n---+---+---\n %c | %c | %c\n---+---+---\n %c | %c | %c\n",
        board[0],board[1],board[2],board[3],board[4],board[5],board[6],board[7],board[8]);
    puts("Draw!");
    return 0;
}
