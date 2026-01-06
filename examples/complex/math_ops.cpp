#include "math_ops.hpp"
#include <ytrace/ytrace.hpp>
#include <thread>
#include <chrono>

namespace math_ops {

double compute_factorial(int n) {
    YTRACE("computing factorial of %d", n);
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    
    double result = 1.0;
    for (int i = 2; i <= n; ++i) {
        result *= i;
        YTRACE("factorial intermediate: %d! = %.0f", i, result);
    }
    
    YTRACE("factorial result: %d! = %.0f", n, result);
    return result;
}

double compute_fibonacci(int n) {
    YTRACE("computing fibonacci(%d)", n);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    if (n <= 1) {
        YTRACE("fibonacci base case: fib(%d) = %d", n, n);
        return n;
    }
    
    double a = 0, b = 1;
    for (int i = 2; i <= n; ++i) {
        double temp = a + b;
        a = b;
        b = temp;
        YTRACE("fibonacci step %d: value = %.0f", i, b);
    }
    
    YTRACE("fibonacci result: fib(%d) = %.0f", n, b);
    return b;
}

double compute_prime_check(int n) {
    YTRACE("checking if %d is prime", n);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    if (n < 2) {
        YTRACE("%d is not prime (less than 2)", n);
        return 0;
    }
    
    for (int i = 2; i * i <= n; ++i) {
        YTRACE("checking divisibility by %d", i);
        if (n % i == 0) {
            YTRACE("%d is divisible by %d, not prime", n, i);
            return 0;
        }
    }
    
    YTRACE("%d is prime", n);
    return 1;
}

} // namespace math_ops
