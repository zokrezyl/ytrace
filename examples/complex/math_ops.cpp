#include "math_ops.hpp"
#include <ytrace/ytrace.hpp>
#include <thread>
#include <chrono>

namespace math_ops {

double compute_factorial(int n) {
    yfunc();
    ytrace("computing factorial of %d", n);
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    
    double result = 1.0;
    for (int i = 2; i <= n; ++i) {
        result *= i;
        ytrace("factorial intermediate: %d! = %.0f", i, result);
    }
    
    ydebug("factorial result: %d! = %.0f", n, result);
    return result;
}

double compute_fibonacci(int n) {
    yfunc();
    ytrace("computing fibonacci(%d)", n);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    if (n <= 1) {
        ytrace("fibonacci base case: fib(%d) = %d", n, n);
        return n;
    }
    
    double a = 0, b = 1;
    for (int i = 2; i <= n; ++i) {
        double temp = a + b;
        a = b;
        b = temp;
        ytrace("fibonacci step %d: value = %.0f", i, b);
    }
    
    ydebug("fibonacci result: fib(%d) = %.0f", n, b);
    return b;
}

double compute_prime_check(int n) {
    yfunc();
    ytrace("checking if %d is prime", n);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    if (n < 2) {
        ywarn("%d is not prime (less than 2)", n);
        return 0;
    }
    
    for (int i = 2; i * i <= n; ++i) {
        ytrace("checking divisibility by %d", i);
        if (n % i == 0) {
            ydebug("%d is divisible by %d, not prime", n, i);
            return 0;
        }
    }
    
    yinfo("%d is prime", n);
    return 1;
}

} // namespace math_ops
