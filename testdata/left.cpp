#include <iostream>

// Returns the sum of two numbers
int add(int a, int b)
{
    // new line
    test();
    return a + b;
}

/* Former implementation:
   no longer in use */
int multiply(int a, int b)
{
    int result = 0;
    for (int i = 0; i < b; ++i)
        result += a;
    return result;
}

int main()
{
    std::cout << add(2, 3) << std::endl;
    std::cout << multiply(4, 5) << std::endl;
    return 0;
}
