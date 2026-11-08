/* Linked against a library that exports VER_2; packing it against a library
 * that only exports VER_1 must fail. */
int v(void);

int main(void)
{
    return v() - 1;
}
