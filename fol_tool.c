#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

/* ==========================================
   跨平台兼容层 (Windows/Linux/macOS)
   ========================================== */
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define PATH_SEP '\\'
#define PATH_SEP_STR "\\"
#define MKDIR(p) _mkdir(p)
#else
#include <dirent.h>
#include <unistd.h>
#define PATH_SEP '/'
#define PATH_SEP_STR "/"
#define MKDIR(p) mkdir(p, 0755)
#endif

#define INDEX_ENTRY_SIZE 136
#define PADDING_COUNT 97

/* ==========================================
   数据结构定义
   ========================================== */

typedef struct
{
    char *full_path; // 物理磁盘上的绝对路径或相对路径
    char *game_path; // 游戏内部路径 (Windows 风格反斜杠)
    uint32_t key;    // 解密密钥
    int index;       // 原始索引顺序
} FileEntry;

typedef struct
{
    FileEntry *items;
    size_t count;
    size_t capacity;
} FileList;

/* ==========================================
   工具函数
   ========================================== */

void init_list(FileList *list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void add_file(FileList *list, const char *full, const char *game, uint32_t key, int index)
{
    if (list->count >= list->capacity)
    {
        list->capacity = (list->capacity == 0) ? 32 : list->capacity * 2;
        list->items = realloc(list->items, list->capacity * sizeof(FileEntry));
    }
    list->items[list->count].full_path = strdup(full);
    list->items[list->count].game_path = strdup(game);
    list->items[list->count].key = key;
    list->items[list->count].index = index;
    list->count++;
}

void free_list(FileList *list)
{
    for (size_t i = 0; i < list->count; i++)
    {
        free(list->items[i].full_path);
        free(list->items[i].game_path);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

/* 路径拼接，自动处理分隔符 */
void path_join(char *dest, const char *p1, const char *p2)
{
    strcpy(dest, p1);
    size_t len = strlen(dest);
    if (len > 0 && dest[len - 1] != PATH_SEP)
    {
        strcat(dest, PATH_SEP_STR);
    }
    strcat(dest, p2);
}

/* 递归创建目录 */
void make_dir_recursive(const char *path)
{
    char tmp[1024];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == PATH_SEP)
        tmp[len - 1] = 0;

    for (p = tmp + 1; *p; p++)
    {
        if (*p == PATH_SEP)
        {
            *p = 0;
            MKDIR(tmp);
            *p = PATH_SEP;
        }
    }
    MKDIR(tmp);
}

/* 统一将路径中的 / 转为 \ (游戏内部格式) */
void sanitize_game_path(char *path)
{
    for (int i = 0; path[i]; i++)
    {
        if (path[i] == '/')
            path[i] = '\\';
    }
}

/* 随机数生成 */
uint32_t rand_u32()
{
    return (uint32_t)((rand() & 0xFFFF) | ((rand() & 0xFFFF) << 16));
}

/* ==========================================
   加解密算法
   ========================================== */

void transform_content(uint8_t *data, size_t len, uint32_t key, int is_encrypt)
{
    size_t num_ints = len / 4;
    uint32_t *ints = (uint32_t *)data;
    for (size_t i = 0; i < num_ints; i++)
    {
        uint32_t term = 99 * (uint32_t)(i * i);
        if (is_encrypt)
            ints[i] = ints[i] + key + term;
        else
            ints[i] = ints[i] - key - term;
    }
}

void transform_index(uint8_t *data, uint32_t key, int is_encrypt)
{
    uint32_t *ints = (uint32_t *)data;
    for (int i = 0; i < 34; i++)
    {
        uint32_t term = 9 * (uint32_t)(i * i * i);
        if (is_encrypt)
            ints[i] = ints[i] + key + term;
        else
            ints[i] = ints[i] - key - term;
    }
}

/* ==========================================
   Manifest 处理 (新格式: Index|Key|Path)
   ========================================== */

// 保存 Manifest 到 manifest.txt
void save_manifest(const char *path, FileList *list)
{
    FILE *f = fopen(path, "w");
    if (!f)
    {
        printf("[!] 错误：无法写入清单文件 %s\n", path);
        return;
    }

    // 写入表头 (注释)
    fprintf(f, "# FOL Manifest\n");
    fprintf(f, "# Format: Index|Key|GamePath\n");

    for (size_t i = 0; i < list->count; i++)
    {
        // 格式: Index|Key|Path
        fprintf(f, "%d|%u|%s\n",
                list->items[i].index,
                list->items[i].key,
                list->items[i].game_path);
    }
    fclose(f);
    printf("[ok] 密钥清单已保存至: %s\n", path);
}

// 读取 Manifest
int load_manifest(const char *path, FileList *out_list)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;

    char line[1024];
    int count = 0;
    while (fgets(line, sizeof(line), f))
    {
        // 跳过注释和空行
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        // 去除换行符
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        {
            line[--len] = 0;
        }

        // 解析 Index|Key|Path
        // 使用 strtok 分割
        char *token_idx = strtok(line, "|");
        char *token_key = strtok(NULL, "|");
        char *token_path = strtok(NULL, "|");

        if (token_idx && token_key && token_path)
        {
            int idx = atoi(token_idx);
            uint32_t key = (uint32_t)strtoul(token_key, NULL, 10);
            // 这里 full_path 暂时留空，稍后打包时会拼接
            add_file(out_list, "", token_path, key, idx);
            count++;
        }
    }
    fclose(f);
    return count;
}

/* ==========================================
   目录扫描 (查找新增文件)
   ========================================== */

void scan_assets_dir(const char *base_path, const char *current_rel, FileList *list)
{
    char search_path[1024];
    char full_path[1024];

#ifdef _WIN32
    WIN32_FIND_DATA fd;
    sprintf(search_path, "%s\\*.*", base_path);
    HANDLE hFind = FindFirstFile(search_path, &fd);

    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        if (strcmp(fd.cFileName, ".DS_Store") == 0)
            continue; // Mac 垃圾文件

        path_join(full_path, base_path, fd.cFileName);

        char rel_path[1024];
        if (strlen(current_rel) == 0)
            strcpy(rel_path, fd.cFileName);
        else
            path_join(rel_path, current_rel, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            scan_assets_dir(full_path, rel_path, list);
        }
        else
        {
            sanitize_game_path(rel_path);
            // full_path 是 assets/xxx, game_path 是 xxx
            add_file(list, full_path, rel_path, 0, 0);
        }
    } while (FindNextFile(hFind, &fd));
    FindClose(hFind);
#else
    DIR *dir = opendir(base_path);
    if (!dir)
        return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (strcmp(entry->d_name, ".DS_Store") == 0)
            continue;

        path_join(full_path, base_path, entry->d_name);
        char rel_path[1024];
        if (strlen(current_rel) == 0)
            strcpy(rel_path, entry->d_name);
        else
            path_join(rel_path, current_rel, entry->d_name);

        struct stat s;
        if (stat(full_path, &s) == 0)
        {
            if (S_ISDIR(s.st_mode))
            {
                scan_assets_dir(full_path, rel_path, list);
            }
            else if (S_ISREG(s.st_mode))
            {
                sanitize_game_path(rel_path);
                add_file(list, full_path, rel_path, 0, 0);
            }
        }
    }
    closedir(dir);
#endif
}

/* ==========================================
   功能：解包 (Unpack)
   ========================================== */

void do_unpack(const char *input_file, const char *workspace_dir)
{
    FILE *f = fopen(input_file, "rb");
    if (!f)
    {
        printf("[!] 无法打开文件: %s\n", input_file);
        return;
    }

    // 读取 Header
    int32_t raw_count;
    if (fread(&raw_count, 4, 1, f) != 1)
        return;

    int is_encrypted = raw_count < 0;
    int count = raw_count & 0x7FFFFFFF;

    printf("[*] 正在解包: %s\n", input_file);
    printf("[*] 文件数量: %d\n", count);

    if (!is_encrypted)
    {
        printf("[!] 错误：目标不是加密的 FOL 文件\n");
        fclose(f);
        return;
    }

    // 准备目录结构
    // workspace_dir/
    //   assets/
    //   manifest.txt
    char assets_dir[1024];
    char manifest_path[1024];

    path_join(assets_dir, workspace_dir, "assets");
    path_join(manifest_path, workspace_dir, "manifest.txt");

    printf("[*] 创建工作空间: %s\n", workspace_dir);
    make_dir_recursive(assets_dir);

    // 读取 Keys
    fseek(f, -4 * (PADDING_COUNT + count), SEEK_END);
    uint32_t *keys = malloc(count * 4);
    fread(keys, 4, count, f);

    // 读取 Index
    fseek(f, 4, SEEK_SET);
    size_t idx_size = count * INDEX_ENTRY_SIZE;
    uint8_t *idx_data = malloc(idx_size);
    fread(idx_data, 1, idx_size, f);

    FileList manifest_list;
    init_list(&manifest_list);

    uint32_t data_base = 4 + idx_size;

    // 遍历并提取
    for (int i = 0; i < count; i++)
    {
        uint32_t key = keys[i];
        uint8_t *entry_ptr = idx_data + (i * INDEX_ENTRY_SIZE);

        transform_index(entry_ptr, key, 0); // 解密索引

        char name[129] = {0};
        memcpy(name, entry_ptr, 128);

        uint32_t offset = *(uint32_t *)(entry_ptr + 128);
        uint32_t size = *(uint32_t *)(entry_ptr + 132);

        if (offset < data_base && offset > 0)
            offset += data_base;

        // 记录到清单
        add_file(&manifest_list, "", name, key, i);

        // 提取文件内容
        fseek(f, offset, SEEK_SET);
        uint8_t *file_buf = malloc(size);
        if (file_buf)
        {
            fread(file_buf, 1, size, f);
            transform_content(file_buf, size, key, 0); // 解密内容

            // 拼接输出路径: assets_dir + name
            char out_path[1024];
            // 将 name 中的 \ 转为本地分隔符
            char local_name[256];
            strcpy(local_name, name);
            for (int k = 0; local_name[k]; k++)
            {
                if (local_name[k] == '\\' || local_name[k] == '/')
                    local_name[k] = PATH_SEP;
            }

            path_join(out_path, assets_dir, local_name);

            // 确保父目录存在
            char *last_sep = strrchr(out_path, PATH_SEP);
            if (last_sep)
            {
                *last_sep = 0;
                make_dir_recursive(out_path);
                *last_sep = PATH_SEP;
            }

            FILE *fo = fopen(out_path, "wb");
            if (fo)
            {
                fwrite(file_buf, 1, size, fo);
                fclose(fo);
            }
            free(file_buf);
        }
    }

    // 保存简化版的 Key 文件
    save_manifest(manifest_path, &manifest_list);

    // 清理
    free(idx_data);
    free(keys);
    free_list(&manifest_list);
    fclose(f);
    printf("[ok] 解包完成！请在 '%s' 目录下查看。\n", workspace_dir);
}

/* ==========================================
   功能：打包 (Pack)
   ========================================== */

int compare_file_idx(const void *a, const void *b)
{
    return ((FileEntry *)a)->index - ((FileEntry *)b)->index;
}

int compare_file_name(const void *a, const void *b)
{
    return strcmp(((FileEntry *)a)->game_path, ((FileEntry *)b)->game_path);
}

void do_pack(const char *workspace_dir, const char *output_file)
{
    char assets_dir[1024];
    char manifest_path[1024];

    path_join(assets_dir, workspace_dir, "assets");
    path_join(manifest_path, workspace_dir, "manifest.txt");

    // 1. 检查工作空间有效性
    struct stat st;
    if (stat(assets_dir, &st) != 0)
    {
        printf("[!] 错误：找不到资源目录 '%s'\n", assets_dir);
        return;
    }
    if (stat(manifest_path, &st) != 0)
    {
        printf("[!] 错误：找不到清单文件 '%s'\n", manifest_path);
        return;
    }

    printf("[*] 正在分析工作空间: %s\n", workspace_dir);

    // 2. 加载 Manifest
    FileList manifest_list;
    init_list(&manifest_list);
    int loaded_count = load_manifest(manifest_path, &manifest_list);
    printf("[*] 已加载密钥清单: %d 条记录\n", loaded_count);

    // 3. 扫描 assets 目录下的实际文件
    FileList disk_files;
    init_list(&disk_files);
    scan_assets_dir(assets_dir, "", &disk_files);
    printf("[*] 扫描到物理文件: %zu 个\n", disk_files.count);

    // 4. 构建最终打包列表 (校验 + 合并)
    FileList final_list;
    init_list(&final_list);

    int *disk_used = calloc(disk_files.count, sizeof(int));

    // Pass 1: 处理 Manifest 中存在的文件 (保持原始 Key 和 顺序)
    // 为了保持原始顺序，Manifest 已经是按 Index 排好序的，或者是乱序的
    // 我们按 Manifest 的读取顺序（通常就是 Index 顺序）来处理
    // 如果需要严格按 Index，可以先 sort manifest_list
    qsort(manifest_list.items, manifest_list.count, sizeof(FileEntry), compare_file_idx);

    for (size_t i = 0; i < manifest_list.count; i++)
    {
        char *target_game_path = manifest_list.items[i].game_path;
        int found_on_disk = -1;

        // 在磁盘列表中查找
        for (size_t j = 0; j < disk_files.count; j++)
        {
            if (!disk_used[j] && strcmp(disk_files.items[j].game_path, target_game_path) == 0)
            {
                found_on_disk = j;
                break;
            }
        }

        if (found_on_disk != -1)
        {
            // 验证通过：文件存在且有 Key
            add_file(&final_list,
                     disk_files.items[found_on_disk].full_path,
                     target_game_path,
                     manifest_list.items[i].key,
                     manifest_list.items[i].index); // 保持原 Index 逻辑顺序，但写入时是追加
            disk_used[found_on_disk] = 1;
        }
        else
        {
            printf("[!] 警告：清单中的文件丢失，将跳过: %s\n", target_game_path);
        }
    }

    // Pass 2: 处理新增文件 (生成随机 Key)
    FileList new_files;
    init_list(&new_files);

    for (size_t j = 0; j < disk_files.count; j++)
    {
        if (!disk_used[j])
        {
            printf("[+] 发现新增文件: %s (生成新密钥)\n", disk_files.items[j].game_path);
            add_file(&new_files,
                     disk_files.items[j].full_path,
                     disk_files.items[j].game_path,
                     rand_u32(),
                     0);
        }
    }
    // 新文件按文件名排序，保证确定性
    qsort(new_files.items, new_files.count, sizeof(FileEntry), compare_file_name);

    for (size_t i = 0; i < new_files.count; i++)
    {
        add_file(&final_list,
                 new_files.items[i].full_path,
                 new_files.items[i].game_path,
                 new_files.items[i].key,
                 0);
    }

    // 5. 开始写入 .fol
    size_t total_files = final_list.count;
    if (total_files == 0)
    {
        printf("[!] 错误：没有文件可打包\n");
        goto cleanup;
    }

    FILE *fo = fopen(output_file, "wb");
    if (!fo)
    {
        printf("[!] 无法创建文件: %s\n", output_file);
        goto cleanup;
    }

    // Step 1: Header
    uint32_t head = (uint32_t)total_files | 0x80000000;
    fwrite(&head, 4, 1, fo);

    // Step 2: 预留 Index 空间
    size_t idx_size = total_files * INDEX_ENTRY_SIZE;
    void *zeros = calloc(1, idx_size);
    fwrite(zeros, 1, idx_size, fo);
    free(zeros);

    uint32_t current_offset = 4 + idx_size;
    uint8_t *idx_buf = calloc(total_files, INDEX_ENTRY_SIZE);
    uint32_t *keys_buf = malloc(total_files * 4);

    printf("[*] 正在写入数据...\n");

    for (size_t i = 0; i < total_files; i++)
    {
        keys_buf[i] = final_list.items[i].key;

        // 读取源文件
        FILE *fi = fopen(final_list.items[i].full_path, "rb");
        if (!fi)
        {
            printf("[!] 读取失败: %s\n", final_list.items[i].full_path);
            continue;
        }
        fseek(fi, 0, SEEK_END);
        long fsize = ftell(fi);
        fseek(fi, 0, SEEK_SET);

        uint8_t *buf = malloc(fsize);
        fread(buf, 1, fsize, fi);
        fclose(fi);

        // 加密内容
        transform_content(buf, fsize, keys_buf[i], 1);
        fwrite(buf, 1, fsize, fo);
        free(buf);

        // 准备索引条目
        uint8_t *entry = idx_buf + (i * INDEX_ENTRY_SIZE);
        // 文件名 (最多127字节)
        strncpy((char *)entry, final_list.items[i].game_path, 127);

        *(uint32_t *)(entry + 128) = current_offset;
        *(uint32_t *)(entry + 132) = (uint32_t)fsize;

        // 加密索引条目
        transform_index(entry, keys_buf[i], 1);

        current_offset += fsize;
    }

    // Step 3: 回写索引
    fseek(fo, 4, SEEK_SET);
    fwrite(idx_buf, 1, idx_size, fo);

    // Step 4: 写入尾部
    fseek(fo, 0, SEEK_END);
    fwrite(keys_buf, 4, total_files, fo);

    void *padding = calloc(PADDING_COUNT, 4);
    fwrite(padding, 1, PADDING_COUNT * 4, fo);
    free(padding);

    printf("[ok] 打包成功！输出文件: %s\n", output_file);

    fclose(fo);
    free(idx_buf);
    free(keys_buf);

cleanup:
    free(disk_used);
    free_list(&new_files);
    free_list(&manifest_list);
    free_list(&disk_files);
    free_list(&final_list);
}

/* ==========================================
   主函数
   ========================================== */

void print_usage(const char *prog)
{
    printf("Pixel Software FOL 工具 (C语言版)\n\n");
    printf("用法:\n");
    printf("  1. 解包 (Unpack)\n");
    printf("     %s unpack <文件.fol> [输出目录名]\n", prog);
    printf("     说明: 将 .fol 解压到一个工作空间目录中，包含 assets 文件夹和 manifest.txt 清单。\n");
    printf("     示例: %s unpack slr.fol slr_project\n\n", prog);

    printf("  2. 打包 (Pack)\n");
    printf("     %s pack <工作空间目录> [输出文件.fol]\n", prog);
    printf("     说明: 读取工作空间下的 manifest.txt 和 assets 目录，重新打包为 .fol 文件。\n");
    printf("     注意: 打包前会自动检查文件完整性。\n");
    printf("     示例: %s pack slr_project slr_new.fol\n", prog);
}

int main(int argc, char **argv)
{
/* --- 新增：解决 Windows 中文乱码问题 --- */
#ifdef _WIN32
    // 设置控制台输出代码页为 UTF-8 (65001)
    SetConsoleOutputCP(65001);
    // 可选：设置控制台输入代码页也为 UTF-8 (防止输入中文参数乱码)
    SetConsoleCP(65001);
#endif
    /* ------------------------------------- */

    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "unpack") == 0)
    {
        if (argc < 3)
        {
            printf("[!] 参数不足。请指定要解包的文件。\n");
            print_usage(argv[0]);
            return 1;
        }
        const char *input = argv[2];
        char out_dir[1024];

        if (argc >= 4)
        {
            strcpy(out_dir, argv[3]);
        }
        else
        {
            // 默认输出目录名: 文件名_project
            strcpy(out_dir, input);
            char *dot = strrchr(out_dir, '.');
            if (dot)
                *dot = 0;
            strcat(out_dir, "_project");
        }
        do_unpack(input, out_dir);
    }
    else if (strcmp(cmd, "pack") == 0)
    {
        if (argc < 3)
        {
            printf("[!] 参数不足。请指定包含 assets 和 manifest.txt 的工作空间目录。\n");
            print_usage(argv[0]);
            return 1;
        }
        const char *input_dir = argv[2];
        char out_file[1024] = "output.fol";

        if (argc >= 4)
        {
            strcpy(out_file, argv[3]);
        }

        do_pack(input_dir, out_file);
    }
    else
    {
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}