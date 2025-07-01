/*
 * Compatibility functions for non-OpenBSD systems
 */

#include <stdlib.h>
#include <string.h>

/*
 * OpenBSD's freezero function - securely free memory
 * For systems that don't have this function
 */
void
freezero(void *ptr, size_t size)
{
    if (ptr == NULL)
        return;
    
    /* Clear the memory before freeing it */
    explicit_bzero(ptr, size);
    free(ptr);
}

/* 
 * If explicit_bzero is also missing, uncomment this:
 */
/*
void
explicit_bzero(void *ptr, size_t size)
{
    memset(ptr, 0, size);
    __asm__ __volatile__ ("" :: "r"(ptr) : "memory");
}
*/

