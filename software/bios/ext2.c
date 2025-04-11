#include "bios_internal.h"
#include "ext2.h"

#define bool int

struct Superblock {
  uint32_t inode_count;
  uint32_t block_count;
  uint32_t reserved_block_count;
  uint32_t free_block_count;
  uint32_t free_inode_count;
  uint32_t first_block;
  uint32_t log_block_size;  // log2(size) - 10
  uint32_t log_fragment_size;
  uint32_t blocks_per_group;
  uint32_t fragments_per_group;
  uint32_t inodes_per_group;
  uint32_t last_mount_time;
  uint32_t last_write_time;
  uint16_t mnt_count;
  uint16_t max_mnt_count;
  uint16_t ext2_signature;  // 0xef53
  uint16_t state;
  uint16_t err_handling;
  uint16_t version_minor;
  uint32_t last_check_time;
  uint32_t check_interval;
  uint32_t os_id;
  uint32_t version_major;
  uint16_t owner_user_id;
  uint16_t owner_group_id;
  // extended superblock (version >= 1)
  uint32_t first_non_reserved_inode;
  uint16_t inode_size;
  // other fields ignored
};

struct GroupDescriptor {
  uint32_t block_usage_block;
  uint32_t inode_usage_block;
  uint32_t inode_table_block;
  uint16_t free_block_count;
  uint16_t free_inode_count;
  uint16_t dir_count;
};

struct DirEntryHeader {
  uint32_t inode;
  uint16_t entry_size;
  unsigned char name_size;
  unsigned char unused;
  char name[0];
};

#define BUFFER_BASE (void*)(RAM_BASE + 0x180000)

static uint32_t partition_start = -1;
static void *mbr_buf                 = BUFFER_BASE;
static struct Superblock *superblock = BUFFER_BASE + 1024;
static void *group_desc_table        = BUFFER_BASE + (1<<12);
static void *inode_buf               = BUFFER_BASE + (2<<12);
static uint32_t *indirect1_buf       = BUFFER_BASE + (3<<12);
static uint32_t *indirect2_buf       = BUFFER_BASE + (4<<12);
static uint32_t *indirect3_buf       = BUFFER_BASE + (5<<12);
static void *dir_buf                 = BUFFER_BASE + (6<<12);
static uint32_t inode_buf_loaded_block;

//#define DEBUG

static uint32_t block_size() { return 1024 << superblock->log_block_size; }
static bool read_block(void* dst, uint32_t block) {
  int sectors = 2 << superblock->log_block_size;
  unsigned first_sector = partition_start + block * sectors;
#ifdef DEBUG
  printf("Reading block %u to %08x (sector=%u, count=%u)\n", block, dst, first_sector, sectors);
#endif
  if (partition_start + block * sectors + sectors >= get_sdcard_sector_count()) {
#ifdef DEBUG
    printf("\tERROR out of range\n");
#endif
    return 0;
  }
  int count = sdread(dst, partition_start + block * sectors, sectors);
  if (count != sectors) printf("ERROR read_block(%u) -> %u of %u sectors\n", block, count, sectors);
  return count == sectors;
}

struct Inode* get_inode(uint32_t inode) {
  if (inode == 0) return 0;
  uint32_t group = (inode - 1) / superblock->inodes_per_group;
  uint32_t index = (inode - 1) % superblock->inodes_per_group;
  struct GroupDescriptor* group_descr = (group_desc_table + group * 32);
  uint32_t group_offset = index * superblock->inode_size;
  uint32_t block_offset = group_offset & (block_size() - 1);
  uint32_t block = group_descr->inode_table_block + (group_offset >> (10 + superblock->log_block_size));
  if (inode_buf_loaded_block != block) {
    if (!read_block(inode_buf, block)) {
      inode_buf_loaded_block = -1;
      return 0;
    }
    inode_buf_loaded_block = block;
  }
  return inode_buf + block_offset;
}

struct BlockIterator {
  const struct Inode* inode;
  int i0, i1, i2, i3;
};

static bool start_block_iter(const struct Inode* inode, struct BlockIterator* iter) {
  iter->inode = inode;
  iter->i0 = iter->i1 = iter->i2 = iter->i3 = 0;
  if (iter->inode->indirect1_block && !read_block(indirect1_buf, iter->inode->indirect1_block)) return 0;
  if (iter->inode->indirect2_block && !read_block(indirect2_buf, iter->inode->indirect2_block)) return 0;
  if (iter->inode->indirect3_block && !read_block(indirect3_buf, iter->inode->indirect3_block)) return 0;
  return 1;
}

static uint32_t next_block(struct BlockIterator* iter) {
  uint32_t max_i = 256 << superblock->log_block_size;
  while (1) {
    if (iter->i0 < 12) {
      uint32_t block = iter->inode->direct_blocks[iter->i0++];
      if (block)
        return block;
      else
        continue;
    }
    if (iter->inode->indirect1_block && iter->i1 < max_i) {
      uint32_t block = indirect1_buf[iter->i1++];
      if (block)
        return block;
      else
        continue;
    }
    if (iter->inode->indirect2_block && iter->i2 < max_i) {
      if (!read_block(indirect1_buf, indirect2_buf[iter->i2++])) return 0;
      iter->i1 = 0;
      continue;
    }
    if (iter->inode->indirect3_block && iter->i3 < max_i) {
      if (!read_block(indirect2_buf, indirect3_buf[iter->i3++])) return 0;
      iter->i2 = 0;
      continue;
    }
    return 0;
  }
}

static uint32_t read_dir(const struct Inode* inode, bool print, const char* search) {
#ifdef DEBUG
  printf(search ? "read_dir(%08x, search='%s')\n" : "read_dir(%08x)\n", inode, search);
#endif
  struct BlockIterator iter;
  uint32_t block;
  if (!start_block_iter(inode, &iter)) return 0;
  while ((block = next_block(&iter))) {
    if (!read_block(dir_buf, block)) return 0;
    int pos = 0;
    while (pos < block_size()) {
      struct DirEntryHeader* h = dir_buf + pos;
      pos += h->entry_size;
      if (h->inode == 0) continue;
      if (print) {
        putchar('*');
        putchar(' ');
        for (int i = 0; i < h->name_size; ++i) putchar(h->name[i]);
        putchar('\n');
      }
      if (search) {
        int i = 0;
        while (i < h->name_size && search[i] == h->name[i]) i++;
        if (i == h->name_size && (search[h->name_size] == 0 || search[h->name_size] == '/')) {
#ifdef DEBUG
          printf("read_dir return inode_id=%u\n", h->inode);
#endif
          return h->inode;
        }
      }
    }
  }
  return 0;
}

#define ROOT_INODE 2

struct Inode* find_inode(const char* path) {
  if (!is_ext2_reader_initialized()) return 0;
#ifdef DEBUG
  printf("find_inode('%s')\n", path);
#endif
  if (*path == '/') path++;
  uint32_t inode_id = ROOT_INODE;
  while (inode_id && *path != 0) {
    struct Inode* inode = get_inode(inode_id);
#ifdef DEBUG
    printf("find_inode id=%u => ptr=%08x\n", inode_id, inode);
#endif
    if (!inode) return 0;
    inode_id = read_dir(inode, 0, path);
    while (*path != 0 && *path != '/') path++;
    if (*path == '/') path++;
  }
#ifdef DEBUG
  printf("find_inode return id=%u\n", inode_id);
#endif
  return get_inode(inode_id);
}

void print_dir(const struct Inode* inode) {
  read_dir(inode, 1, 0);
}

uint32_t read_file(const struct Inode* inode, void* dst, uint32_t max_size) {
  struct BlockIterator iter;
  uint32_t block;
  uint32_t size_done = 0;
  if (!start_block_iter(inode, &iter)) return 0;
  while ((block = next_block(&iter)) && size_done < max_size) {
    uint32_t sectors = 2 << superblock->log_block_size;
    uint32_t first_sector = block * sectors;
    uint32_t sectors_limit = (max_size - size_done + 511) >> 9;
    if (sectors > sectors_limit) sectors = sectors_limit;
    if (sdread(dst, partition_start + first_sector, sectors) != sectors) return size_done;
    dst += (sectors << 9);
    size_done += (sectors << 9);
  }
  return size_done < max_size ? size_done : max_size;
}

bool is_ext2_reader_initialized() { return partition_start != -1; }

static const char* init_ext2_reader(unsigned start_sector) {
  partition_start = -1;
  if (get_sdcard_sector_count() < start_sector + 4) {
    return "No SD card or invalid start sector";
  }
  if (sdread((void*)superblock, start_sector + 2, 2) != 2) {
    return "IO error";
  }
  if (superblock->ext2_signature != 0xef53) {
    return "No EXT2 signature";
  }
  partition_start = start_sector;
  if (superblock->version_major == 0) superblock->inode_size = 128;
  if (superblock->inode_size & 3) {
    printf("Invalid inode_size %u\n", superblock->inode_size);
    return "Bad superblock";
  }
  if (superblock->log_block_size > 2) {
    printf("Invalid log_block_size %u\n", superblock->log_block_size);
    return "Bad superblock";
  }
  inode_buf_loaded_block = -1;
  if (!read_block(group_desc_table, superblock->log_block_size ? 1 : 2)) {
    partition_start = -1;
    return "IO error";
  }
  struct Inode* root_inode = get_inode(ROOT_INODE);
  if (!root_inode || !is_dir(root_inode)) {
    partition_start = -1;
    return "Root dir not found";
  }
  return 0;
}

int search_and_select_ext2_fs() {
  if (sdread(mbr_buf, 0, 1) != 1) {
    printf("\tIO error\n");
    return -1;
  }
  if (init_ext2_reader(0) == 0) return 0;
  const unsigned short* partition_entry = mbr_buf + 0x01be;
  for (int i = 0; i < 4; ++i) {
    unsigned start_sector = ((unsigned)partition_entry[5] << 16) | partition_entry[4];
    unsigned size = ((unsigned)partition_entry[7] << 16) | partition_entry[6];
    if (start_sector == 0 || size == 0) continue;
    if (init_ext2_reader(start_sector) == 0) return i + 1;
    partition_entry += 8;
  }
  return -1;
}

int select_ext2_fs(int partition) {
  if (partition < 0 || partition > 4) return -1;
  unsigned start_sector = 0;
  if (partition > 0) {
    if (sdread(mbr_buf, 0, 1) != 1) {
      printf("IO error\n");
      return -1;
    }
    const unsigned short* partition_entry = mbr_buf + 0x01be + (partition-1) * 16;
    start_sector = ((unsigned)partition_entry[5] << 16) | partition_entry[4];
    unsigned size = ((unsigned)partition_entry[7] << 16) | partition_entry[6];
    if (start_sector == 0 || size == 0) {
      printf("Partition %u doesn't exist\n", partition);
      return -1;
    }
  }
  const char* err = init_ext2_reader(start_sector);
  if (err) {
    printf("%s\n", err);
    return -1;
  }
  return 0;
}
