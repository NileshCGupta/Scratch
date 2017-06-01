#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include "ext2_fs.h"

ssize_t SUPERBLOCK_OFFSET = 1024;
ssize_t BLKGROUP_TABLE_OFFSET;

char* imgfile;
char* buff;
int img_fd;
off_t filesize;

// file system descriptor vars
__u32 num_blocks, num_inodes, num_free_blocks, num_free_inodes,
		block_size, inode_size, blocks_per_group, inodes_per_group, 
		first_free_inode, num_groups;

__u32 group_index, num_free_blocks, num_free_inodes, bbitmap_index, 
			ibitmap_index, inodes_bitmap_index;

// void printdatetime(__u32 seconds)
// {
// 	int day = seconds % (3600*24*365) / (3600*24);
// 	int year = seconds / (3600*24*365);
// 	int month = seconds % (3600*24*265) / 
// }

void superblock()
{
	struct ext2_super_block *s_block;
	s_block = malloc(sizeof(struct ext2_super_block));

	// Read entire super block in
	pread(img_fd, s_block, 1024, SUPERBLOCK_OFFSET);

	num_inodes = s_block->s_inodes_count;
	num_blocks = s_block->s_blocks_count;
	block_size = filesize / num_blocks;
	inode_size = s_block->s_inode_size;
	blocks_per_group = s_block->s_blocks_per_group;
	inodes_per_group = s_block->s_inodes_per_group;
	first_free_inode = s_block->s_first_ino;

	printf("SUPERBLOCK,%d,%d,%d,%d,%d,%d,%d\n", num_blocks, num_inodes, 
			block_size, inode_size, blocks_per_group, inodes_per_group, 
			first_free_inode);

	num_groups = 1 + ((num_blocks - 1) / blocks_per_group);
}

void group()
{
	if (block_size == EXT2_MIN_BLOCK_SIZE)
		BLKGROUP_TABLE_OFFSET = block_size * 2;
	else
		BLKGROUP_TABLE_OFFSET = block_size;

	struct ext2_group_desc *gd;
	gd = malloc(sizeof(struct ext2_group_desc));
	pread(img_fd, gd, sizeof(struct ext2_group_desc), BLKGROUP_TABLE_OFFSET);

	// ASSUME ONLY 1 GROUP
	group_index = 0;
	bbitmap_index = gd->bg_block_bitmap;
	ibitmap_index = gd->bg_inode_bitmap;
	num_free_blocks = gd->bg_free_blocks_count;
	num_free_inodes = gd->bg_free_inodes_count;
	inodes_bitmap_index = gd->bg_inode_table;

	printf("GROUP,%d,%d,%d,%d,%d,%d,%d,%d\n", group_index, num_blocks, num_inodes, 
			num_free_blocks, num_free_inodes, bbitmap_index, ibitmap_index, 
			inodes_bitmap_index);
}

void bfree()
{
	__u8 *byte;
	byte = malloc(sizeof(char));

	for (int i = 0; i < num_blocks; i++)
	{
		pread(img_fd, byte, 1, bbitmap_index*block_size + (i/8));

		if (!(*byte >> (i%8) & 0x01))
			printf("BFREE,%d\n", i+1);
	}
}

void ifree()
{
	__u8 *byte;
	byte = malloc(sizeof(char));

	for (int i = 0; i < num_inodes; i++)
	{
		pread(img_fd, byte, 1, ibitmap_index*block_size + (i/8));

		if (!(*byte >> (i%8) & 0x01))
			printf("IFREE,%d\n", i+1);
	}
}

void printdatetime(struct tm *a_time) 
{
	printf("%02d/%02d/%02d %02d:%02d:%02d", (a_time->tm_mon)+1, 
					a_time->tm_mday, (a_time->tm_year)%100, a_time->tm_hour,
					a_time->tm_min, a_time->tm_sec);
}

void indirect_helper(int *first_data_block, int first_block, int own_file, const int block_ptr, int level)
{
	void *block;
	block = malloc(block_size);
	int offset = block_ptr * block_size;
	pread(img_fd, block, block_size, offset);
	__u32 *block_num;
	int first_block_flag = 0;
	
	int i = 0;

	while (i < block_size) 
	{
		block_num = (__u32 *)(block + i);	
		if (*block_num != 0) {
			long int file_offset;
			if (level > 1)	{
				indirect_helper(first_data_block, first_block, 
							own_file, *block_num, --level); // RECURSION
				file_offset = (*first_data_block > 0)? (*first_data_block - first_block): *block_num - first_block;
			}
			else	{
				file_offset = *block_num - first_block;
				if (!first_block_flag)	{
					*first_data_block = *block_num;
					first_block_flag = 1;
				}
			}
			printf("INDIRECT,%d,%d,%ld,%d,%d\n", own_file, 
				level, --file_offset, block_ptr, *block_num);
		}
		i += sizeof(__u32);
	}
}

int get_indirects(int block_ptr, int level, int *indirects_array, int start)
{
	void *block;
	block = malloc(block_size);
	int offset = block_ptr * block_size;
	pread(img_fd, block, block_size, offset);
	__u32 *block_num;

	int *indirects = indirects_array;
	int count = 0;
	
	int i = 0;

	while (i < block_size) 
	{
		block_num = (__u32 *)(block + i);	
		if ((*block_num) != 0) {
			if (level > 1)	{
				count += get_indirects(*block_num, --level, indirects, start);
			}
			else	{
				*(indirects + count + start) = *block_num;	
				count++;	
			}
		}
		i += sizeof(__u32);
	}

	return count;
}

void inode()
{
	struct tm *a_time, *m_time, *c_time;

	int isize = sizeof(struct ext2_inode);
	struct ext2_inode *inode;
	inode = malloc(isize);

	char* file_type = malloc(sizeof(char));

	__u32 mode, owner, group, link_count,  
		file_size, num_blocks;

	for (int i = 0; i < inodes_per_group; i++)
	{
		pread(img_fd, inode, isize, 
				inodes_bitmap_index*block_size + i*isize);

		if (inode->i_mode != 0 && inode->i_links_count != 0)
		{
			int ft = ((inode->i_mode) & 0xF000) >> 12;
			switch (ft)
			{
				case 12:
					*file_type = 's';
					break;
				case 8:
					*file_type = 'f';
					break;
				case 4:
					*file_type = 'd';
					break;
				default:
					*file_type = '?';
					break;
			}

			mode = (inode->i_mode) & 0x0FFF;
			owner = inode->i_uid;
			group = inode->i_gid;
			link_count = inode->i_links_count;

			printf("INODE,%d,%s,%o,%d,%d,%d,", i+1, file_type, mode, owner, 
				group, link_count);

			// change time
			time_t c = (time_t) (inode->i_ctime);
			c_time = gmtime(&c);
			printdatetime(c_time);
			printf(",");
			// modify time
			time_t m = (time_t) (inode->i_mtime);
			m_time = gmtime(&m);
			printdatetime(m_time);
			printf(",");
			// access time
			time_t a = (time_t) (inode->i_atime);
			a_time = gmtime(&a);
			printdatetime(a_time);
			printf(",");

			__u32 i_size = inode->i_size;
			__u32 i_blocks = inode->i_blocks;

			printf("%d,%d", i_size, i_blocks);	

			int first_block = inode->i_block[0];
			for (int j = 0; j < 15; j++)	{
				if (inode->i_block[j] < first_block && inode->i_block[j] != 0 && j < 12)
					first_block = inode->i_block[j];
				printf(",%d", inode->i_block[j]);
			}

			printf("\n");
			first_block = (first_block > 12)? first_block: 12;

			for (int j = 12; j < 15; j++)	{
				int num = inode->i_block[j];
				int *tmp = malloc(sizeof(int));
				*tmp = 0;
				if (num != 0)
					indirect_helper(tmp, first_block, i+1, num, j-11);
			}	

			if (*file_type == 'd') {

				int max_index = i_blocks / (block_size / 512);
				int d_size = sizeof(struct ext2_dir_entry);

				struct ext2_dir_entry *entry;
				entry = malloc(d_size);
				int *indirects_array = malloc((max_index - 12) * sizeof(int));
				int indirects_flag = 0;

				for (int k = 0; k < max_index; k++)
				{
					int cur_block = 0;				

					if (k < 13 && !indirects_flag)
						cur_block = inode->i_block[k];

					// Get all indirectly pointed to data blocks
					if (k > 11 && !indirects_flag && inode->i_block[12])	{				
						int index = 0;
						if (inode->i_block[12])
							index += get_indirects(cur_block, 1, indirects_array, index);
						if (inode->i_block[13])
							index += get_indirects(cur_block, 2, indirects_array, index);
						if (inode->i_block[14])
							index += get_indirects(cur_block, 3, indirects_array, index);
						indirects_flag = 1;
					}

					unsigned int l = 0;
					while (l < block_size)
					{
						cur_block = (k > 11)? (*(indirects_array+(k-12))): inode->i_block[k];
						if (cur_block == 0)
							break;
						int offset = cur_block * block_size;
						offset += l;	// access within a block
						pread(img_fd, entry, sizeof(struct ext2_dir_entry), offset);
						if (entry->name_len != 0)	{
							printf("DIRENT,%d,%d,%d,%d,%d,'%s'\n", i+1, l, entry->inode, 
									entry->rec_len, entry->name_len, entry->name);
						}
						l += entry->rec_len;
					}
				}
			} 
		}
	}
}

int main(int argc, char* argv[])
{	
	if (argc > 2)	{
		perror("Too many arguments");
		exit(1);	
	}
	else
		imgfile = argv[1];

	img_fd = open(imgfile, O_RDONLY, S_IRUSR);

	if (img_fd < 0)	{
		perror("Cannot open img file");
		exit(1);
	}

	struct stat st;
	if (stat(imgfile, &st) == 0)
		filesize = st.st_size;

	superblock();
	group();
	bfree();
	ifree();
	inode();
	
	exit(0);
}