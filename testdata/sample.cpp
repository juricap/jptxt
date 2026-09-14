#include <stdio.h>
#include <stdint.h>

/* block comment
   continues
*/
int main(int argc, char** argv) {
    const char* s = "hello, jptxt";
    int n = 42;
    // line comment
    if (argc > 1) {
        printf("%s %d\n", s, n);
    }
    return 0;
}
