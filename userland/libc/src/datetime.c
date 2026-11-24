#include <denos/datetime.h>
int den_is_leap(int y){return(y%4==0&&y%100!=0)||y%400==0;}
int den_days_in_month(int y,int m){static const int d[]={31,28,31,30,31,30,31,31,30,31,30,31};if(m<1||m>12)return 0;return d[m-1]+(m==2&&den_is_leap(y));}
int den_valid_date(int y,int m,int d){return y>=1&&m>=1&&m<=12&&d>=1&&d<=den_days_in_month(y,m);}
