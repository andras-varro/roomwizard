/*
 * devmem_write.c - Write to physical memory via mmap on /dev/mem
 *
 * Cross-compile: arm-linux-gnueabihf-gcc -static -O2 -o devmem_write devmem_write.c
 *
 * Usage: devmem_write <phys_addr_hex> <value_hex>
 * Example: devmem_write 0x80851b58 0xc01aa83c
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <phys_addr> [value_to_write]\n", argv[0]);
        fprintf(stderr, "  Read:  %s 0x80851b58\n", argv[0]);
        fprintf(stderr, "  Write: %s 0x80851b58 0xc01aa83c\n", argv[0]);
        return 1;
    }

    uint32_t phys_addr = strtoul(argv[1], NULL, 0);
    int do_write = (argc >= 3);
    uint32_t write_val = 0;
    if (do_write)
        write_val = strtoul(argv[2], NULL, 0);

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }

    /* Page-align the address */
    uint32_t page_size = sysconf(_SC_PAGE_SIZE);
    uint32_t page_mask = ~(page_size - 1);
    uint32_t page_base = phys_addr & page_mask;
    uint32_t page_offset = phys_addr - page_base;

    /* mmap the page */
    void *map = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, page_base);
    if (map == MAP_FAILED) {
        fprintf(stderr, "mmap(phys=0x%08x, size=0x%x) failed: %s\n",
                page_base, page_size, strerror(errno));
        close(fd);
        return 1;
    }

    volatile uint32_t *ptr = (volatile uint32_t *)((uint8_t *)map + page_offset);

    /* Read current value */
    uint32_t cur_val = *ptr;
    printf("Address 0x%08x: current value = 0x%08x\n", phys_addr, cur_val);

    if (do_write) {
        printf("Writing 0x%08x to 0x%08x...\n", write_val, phys_addr);
        *ptr = write_val;

        /* Verify */
        uint32_t new_val = *ptr;
        printf("Readback: 0x%08x %s\n", new_val,
               (new_val == write_val) ? "OK" : "MISMATCH!");

        if (new_val != write_val) {
            fprintf(stderr, "ERROR: Write verification failed!\n");
            munmap(map, page_size);
            close(fd);
            return 1;
        }
    }

    munmap(map, page_size);
    close(fd);
    return 0;
}
