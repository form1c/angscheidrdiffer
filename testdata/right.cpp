#include <iostream>
#include <string>

// Returns the sum of two integers
int add(int a, int b)
{
    test(a);
    return a + b;
}

int multiply(int a, int b)
{
    return a * b;
}

std::string greet(const std::string &name)
{
    return "Hello " + name;
}

int main()
{
    std::cout << add(2, 3) << std::endl;
    std::cout << multiply(4, 5) << std::endl;
    std::cout << greet("World") << std::endl;
    return 0;
}
