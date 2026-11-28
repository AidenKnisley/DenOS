#include <stdio.h>
int main(void) {
    int world[800];
    int future[800];
    int generation;
    int row;
    int col;
    int dx;
    int dy;
    int count;
    for (row=0; row<800; row++) { world[row]=0; future[row]=0; }
    world[1*40+2]=1; world[2*40+3]=1;
    world[3*40+1]=1; world[3*40+2]=1; world[3*40+3]=1;
    world[8*40+8]=1; world[8*40+9]=1; world[8*40+10]=1;
    for (generation=0; generation<20; generation++) {
        printf("\nDenOS Life - generation %d\n", generation);
        for (row=0; row<20; row++) {
            for (col=0; col<40; col++) {
                if (world[row*40+col]) putchar('#');
                else putchar(' ');
            }
            putchar('\n');
        }
        for (row=0; row<20; row++) {
            for (col=0; col<40; col++) {
                count=0;
                for (dy=-1; dy<=1; dy++) {
                    for (dx=-1; dx<=1; dx++) {
                        if ((dx!=0 || dy!=0) && row+dy>=0 && row+dy<20 && col+dx>=0 && col+dx<40) {
                            count=count+world[(row+dy)*40+(col+dx)];
                        }
                    }
                }
                if (count==3) future[row*40+col]=1;
                else if (world[row*40+col] && count==2) future[row*40+col]=1;
                else future[row*40+col]=0;
            }
        }
        for (row=0; row<800; row++) world[row]=future[row];
    }
    return 0;
}
