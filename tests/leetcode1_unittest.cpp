#include <stdio.h>
#include <string.h>
#include <string>
#include <math.h>

class Solution 
{
public:
    int primePalindrome(long int N) 
    {
        // fprintf(stdout, "isHui : %d\n", isHui(N) ? 1 : 0);
        // fprintf(stdout, "isPrime : %d\n", isPrime(N) ? 1 : 0);

        for (long int i = N; i < 9989900;i++)
        {
            if (isPrePrime(i) && isHui(i) && isPrime(i))
            {
                return i;
            }
        }

        return 100030001;
    }

    bool isPrePrime(long int n)
    {
        if (n <= 1)
        {
            return false;
        }

        if (n == 2 || n == 3 || n == 5)
        {
            return true;
        }

        int t0 = n % 10;
        if (t0 == 2 || t0 == 4 || t0 == 5 || t0 == 6 || t0 == 8)
        {
            return false;
        }

        int t1 = n % 6;
        if (t1 != 1 && t1 != 5)
        {
            return false;
        }

        return true;
    }

    bool isPrime(long int n)
    {
        long int s = sqrt(n);

        for (long int i = 5; i <= s; i += 6)
        {
            if (n % i == 0 || n % (i+2) == 0)
		    {
			    return false;
            }
        }

        return true;
    }

    bool isHui(long int n)
    {
        static long int numLen[] = {1,10,100,1000,10000,100000,1000000,10000000,100000000,1000000000};
        long int numLen_len = 10;

        long int len = Len(n);
        // fprintf(stdout, "len : %d\n", len);

        long int loc = 0;
        while (loc < (len+1)/2)
        {
            long int left = (n / numLen[loc]) % 10;
            long int right = (n / numLen[len - loc - 1]) % 10;
            if (left != right)
            {
                return false;
            }
            loc++;
        }

        return true;
    }

    inline int Len(long int n)
    {
        static long int numLen[] = {1,10,100,1000,10000,100000,1000000,10000000,100000000,1000000000};
        long int numLen_len = 10;

        long int l = numLen_len - 1;
        while (l >= 0 && n <= numLen[l]) l--;

        return l + 1;
    }
};

int main()
{
    Solution s;
    // int i = s.primePalindrome(13);
    // fprintf(stdout, "value : %d\n", i);
    // i = s.primePalindrome(6);
    // fprintf(stdout, "value : %d\n", i);
    // int i = s.primePalindrome(9989900);
    // fprintf(stdout, "value : %d\n", i);
    int i = s.primePalindrome(10000);
    fprintf(stdout, "value : %d\n", i);

    return 0;
}