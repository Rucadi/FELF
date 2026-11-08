int ghost_value(void);

/* Uses libghost, so the dependency is kept even with --as-needed. */
int needy(void)
{
    return 1 + ghost_value();
}
