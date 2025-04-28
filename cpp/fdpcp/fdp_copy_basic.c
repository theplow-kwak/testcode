/*
 * FDP Copy Basic Tool - Initial Version
 * Author: (User)
 * Description: Basic NVMe FDP Copy admin command sender using PRP List.
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <stdint.h>
 #include <string.h>
 #include <fcntl.h>
 #include <unistd.h>
 #include <errno.h>
 #include <sys/ioctl.h>
 #include <linux/nvme_ioctl.h>
 #include <linux/nvme.h>
 
 #define PAGE_SIZE 4096
 
 struct nvme_copy_descriptor {
     uint64_t slba: 63;
     uint64_t rsvd1: 1;
     uint16_t nlb;
     uint16_t rsvd2;
     uint32_t rsvd3;
 } __attribute__((packed));
 
 void *alloc_prp_aligned_buffer(size_t size) {
     void *ptr = NULL;
     if (posix_memalign(&ptr, PAGE_SIZE, size) != 0) {
         return NULL;
     }
     memset(ptr, 0, size);
     return ptr;
 }
 
 int send_fdp_copy(int fd, uint32_t nsid, struct nvme_copy_descriptor *descs, uint32_t desc_count, uint64_t dst_lba) {
     struct nvme_passthru_cmd cmd = {0};
 
     cmd.opcode = 0x19; // Copy Command
     cmd.nsid = nsid;
     cmd.addr = (__u64)(uintptr_t)descs;
     cmd.data_len = desc_count * sizeof(struct nvme_copy_descriptor);
 
     cmd.cdw10 = ((desc_count - 1) & 0xFFF) | ((0 & 0xF) << 20); // Copy Type = 0
     cmd.cdw11 = dst_lba & 0xFFFFFFFF;
     cmd.cdw12 = dst_lba >> 32;
 
     int err = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
     if (err < 0) {
         perror("ioctl NVME_IOCTL_ADMIN_CMD (Copy)");
         return -1;
     }
 
     return 0;
 }
 
 int main(int argc, char *argv[]) {
     if (argc != 2) {
         printf("Usage: %s /dev/nvme0\n", argv[0]);
         return -1;
     }
 
     const char *dev_path = argv[1];
     int fd = open(dev_path, O_RDWR);
     if (fd < 0) {
         perror("open");
         return -1;
     }
 
     struct nvme_copy_descriptor *descs = (struct nvme_copy_descriptor *)alloc_prp_aligned_buffer(PAGE_SIZE);
     if (!descs) {
         perror("alloc_prp_aligned_buffer");
         close(fd);
         return -1;
     }
 
     descs[0].slba = 0x1000; // Source LBA
     descs[0].nlb = 8;       // Number of LBAs to copy
     descs[0].rsvd1 = 0;
     descs[0].rsvd2 = 0;
     descs[0].rsvd3 = 0;
 
     uint64_t dst_lba = 0x2000; // Destination LBA
 
     if (send_fdp_copy(fd, 1, descs, 1, dst_lba) != 0) {
         printf("Copy command failed\n");
     } else {
         printf("Copy command successful\n");
     }
 
     free(descs);
     close(fd);
     return 0;
 }
 