#include <denos/math.h>
double den_fabs(double x){return x<0?-x:x;}
double den_floor(double x){long long i=(long long)x;return (double)(x<(double)i?i-1:i);}
double den_ceil(double x){long long i=(long long)x;return (double)(x>(double)i?i+1:i);}
double den_sqrt(double x){if(x<0)return 0;if(x==0)return 0;double r=x>1?x:1;for(int i=0;i<48;i++)r=0.5*(r+x/r);return r;}
double den_pow_i(double x,int n){double r=1;unsigned int p=n<0?(unsigned int)(-(n+1))+1u:(unsigned int)n;while(p){if(p&1u)r*=x;x*=x;p>>=1;}return n<0?1.0/r:r;}
