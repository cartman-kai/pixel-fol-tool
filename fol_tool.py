import struct
import os
import sys
import random
import argparse
import json
import binascii

# ============================
# 核心加密/解密类
# ============================
class FolCrypto:
    @staticmethod
    def transform_content(data_bytes, key, is_encrypt):
        """内容加解密 (Raw +/- (Key + 99*i^2))"""
        num_ints = len(data_bytes) // 4
        ints = list(struct.unpack('<' + 'I' * num_ints, data_bytes[:num_ints*4]))
        result_data = bytearray()
        
        for i in range(num_ints):
            val = ints[i]
            term = (99 * i * i) & 0xFFFFFFFF
            if is_encrypt:
                new_val = (val + key + term) & 0xFFFFFFFF
            else:
                new_val = (val - key - term) & 0xFFFFFFFF
            result_data += struct.pack('<I', new_val)
            
        # 处理尾部余数
        remainder = len(data_bytes) % 4
        if remainder > 0:
            result_data += data_bytes[-remainder:]
            
        return result_data

    @staticmethod
    def transform_index(data_bytes, key, is_encrypt):
        """索引条目加解密 (Raw +/- (Key + 9*i^3))"""
        if len(data_bytes) != 136: return data_bytes
        ints = list(struct.unpack('<' + 'I' * 34, data_bytes))
        result_data = bytearray()
        for i in range(34):
            val = ints[i]
            term = (9 * (i ** 3)) & 0xFFFFFFFF
            if is_encrypt:
                new_val = (val + key + term) & 0xFFFFFFFF
            else:
                new_val = (val - key - term) & 0xFFFFFFFF
            result_data += struct.pack('<I', new_val)
        return result_data

# ============================
# 密钥清单管理
# ============================
class FolKeyManager:
    @staticmethod
    def save_manifest(file_path, entries):
        try:
            sorted_entries = sorted(entries, key=lambda x: x['index'])
            with open(file_path, 'w', encoding='utf-8') as f:
                json.dump(sorted_entries, f, indent=2, ensure_ascii=False)
            print(f"[+] 密钥清单已保存: {file_path}")
        except Exception as e:
            print(f"[!] 保存清单失败: {e}")

    @staticmethod
    def load_manifest(file_path):
        if not os.path.exists(file_path): return None
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                return json.load(f)
        except Exception as e:
            print(f"[!] 读取清单失败: {e}")
            return None

# ============================
# 打包器 (修复版)
# ============================
class FolPacker:
    def __init__(self, source_folder, output_file, key_file=None):
        self.source_folder = source_folder
        self.output_file = output_file
        self.key_file = key_file
        self.files_to_pack = []

    def pack(self):
        if not os.path.isdir(self.source_folder): 
            print(f"[!] 错误：目录不存在 {self.source_folder}")
            return
        
        # 1. 准备文件列表
        disk_files = self._scan_disk_files()
        manifest = None
        if self.key_file:
            manifest = FolKeyManager.load_manifest(self.key_file)
        
        self._build_pack_list(disk_files, manifest)
        file_count = len(self.files_to_pack)
        
        if file_count == 0:
            print("[!] 没有文件需要打包")
            return

        print(f"[*] 开始打包 {file_count} 个文件...")

        try:
            with open(self.output_file, 'wb') as f_out:
                # --- Step 1: 写入文件头 ---
                header_val = file_count | 0x80000000
                f_out.write(struct.pack('<I', header_val))
                
                # --- Step 2: [关键修复] 预留索引表空间 ---
                # 必须先写空字节占位，否则写入数据时会从 offset 4 开始，
                # 导致计算的偏移量与实际位置不符，且后续回写索引时会覆盖数据！
                index_table_size = file_count * 136
                print(f"[*] 预留索引空间: {index_table_size} bytes")
                f_out.write(b'\x00' * index_table_size)
                
                # 此时文件指针应该正好在 Data 区的开始位置
                current_data_offset = f_out.tell()
                
                # 理论上的起始位置应该是 4 + Count*136
                expected_data_start = 4 + index_table_size
                if current_data_offset != expected_data_start:
                    print(f"[!] 致命错误：文件指针位置异常！当前: {current_data_offset}, 预期: {expected_data_start}")
                    return

                index_buffer = bytearray()
                keys_list = []
                
                print("[*] 正在加密并写入文件内容...")
                
                # --- Step 3: 写入文件内容 ---
                for i, file_info in enumerate(self.files_to_pack):
                    key = file_info['key']
                    keys_list.append(key)
                    
                    with open(file_info['full_path'], 'rb') as f_in:
                        plain_data = f_in.read()
                    
                    # 加密
                    enc_data = FolCrypto.transform_content(plain_data, key, True)
                    
                    # 写入
                    f_out.write(enc_data)
                    file_size = len(enc_data)
                    
                    # 创建索引条目 (使用当前绝对偏移量)
                    entry = self._create_index_entry(file_info['game_path'], current_data_offset, file_size, key)
                    index_buffer += entry
                    
                    current_data_offset += file_size
                
                # --- Step 4: 回写索引表 ---
                print("[*] 回写加密索引表...")
                f_out.seek(4) # 跳过 Header
                f_out.write(index_buffer)
                
                # --- Step 5: 写入尾部 (Keys + Padding) ---
                f_out.seek(0, 2) # 跳到文件最末尾
                print("[*] 写入密钥表和尾部填充...")
                
                for key in keys_list:
                    f_out.write(struct.pack('<I', key))
                
                # Padding
                f_out.write(b'\x00' * (97 * 4))
                
                final_pos = f_out.tell()
                print(f"[+] 打包完成: {self.output_file}")
                print(f"[+] 最终文件大小: {final_pos}")
                
                # 校验大小
                expected_size = 4 + index_table_size + (current_data_offset - expected_data_start) + (len(keys_list)*4) + (97*4)
                if final_pos == expected_size:
                     print(f"[SUCCESS] 文件大小校验通过！")
                else:
                     print(f"[WARNING] 文件大小校验不通过！预期: {expected_size}, 实际: {final_pos}")

        except Exception as e:
            print(f"[!] 打包错误: {e}")
            import traceback
            traceback.print_exc()

    def _scan_disk_files(self):
        files_map = {}
        for root, dirs, files in os.walk(self.source_folder):
            for file in files:
                if file.endswith(".key") or file.endswith(".json") or file == ".DS_Store": continue
                full_path = os.path.join(root, file)
                rel_path = os.path.relpath(full_path, self.source_folder)
                game_path = rel_path.replace('/', '\\')
                files_map[game_path] = full_path
        return files_map

    def _build_pack_list(self, disk_files_map, manifest):
        processed = set()
        if manifest:
            manifest.sort(key=lambda x: x['index'])
            for entry in manifest:
                g_path = entry['name']
                if g_path in disk_files_map:
                    self.files_to_pack.append({
                        'full_path': disk_files_map[g_path],
                        'game_path': g_path,
                        'key': entry['key']
                    })
                    processed.add(g_path)
        
        # 新增文件
        new_files = []
        for g_path, f_path in disk_files_map.items():
            if g_path not in processed:
                print(f"[*] 发现新文件: {g_path}")
                new_files.append({
                    'full_path': f_path,
                    'game_path': g_path,
                    'key': random.randint(0, 0xFFFFFFFF)
                })
        new_files.sort(key=lambda x: x['game_path'])
        self.files_to_pack.extend(new_files)

    def _create_index_entry(self, filename, offset, size, key):
        entry_buffer = bytearray(136)
        try:
            name_bytes = filename.encode('gbk')
        except:
            name_bytes = filename.encode('ascii', errors='ignore')
        if len(name_bytes) > 127: name_bytes = name_bytes[:127]
        
        entry_buffer[0:len(name_bytes)] = name_bytes
        struct.pack_into('<I', entry_buffer, 128, offset)
        struct.pack_into('<I', entry_buffer, 132, size)
        
        return FolCrypto.transform_index(entry_buffer, key, True)

# ============================
# 解包器
# ============================
class FolExtractor:
    def __init__(self, input_file, output_dir):
        self.input_file = input_file
        self.output_dir = output_dir
        self.keys = []
        self.manifest_data = []

    def extract(self):
        if not os.path.exists(self.input_file): 
            print(f"[!] 文件不存在: {self.input_file}")
            return
        try:
            with open(self.input_file, 'rb') as f:
                header = f.read(4)
                raw_count = struct.unpack('<i', header)[0]
                count = raw_count & 0x7FFFFFFF
                is_encrypted = raw_count < 0
                
                print(f"[*] 打开文件: {self.input_file}")
                print(f"[*] 文件数量: {count}")
                
                if not is_encrypted:
                    print("[!] 不是加密的 FOL 文件")
                    return

                # 读取 Key (从末尾往前推)
                offset_from_end = 4 * (-97 - count)
                f.seek(offset_from_end, 2)
                keys_data = f.read(4 * count)
                self.keys = struct.unpack('<' + 'I' * count, keys_data)
                
                # 读取 Index (从头部 offset 4 开始)
                f.seek(4, 0)
                index_data = f.read(count * 136)
                
                entries = self._parse_index(index_data, count)
                
                print(f"[*] 正在提取到: {self.output_dir}")
                if not os.path.exists(self.output_dir): os.makedirs(self.output_dir)
                
                for i, entry in enumerate(entries):
                    key = self.keys[i]
                    f.seek(entry['offset'])
                    file_data = f.read(entry['size'])
                    
                    dec_data = FolCrypto.transform_content(file_data, key, False)
                    
                    safe_name = entry['name'].replace('\\', os.sep).replace('/', os.sep)
                    out_path = os.path.join(self.output_dir, safe_name)
                    os.makedirs(os.path.dirname(out_path), exist_ok=True)
                    
                    with open(out_path, 'wb') as f_out:
                        f_out.write(dec_data)
                
                FolKeyManager.save_manifest(self.output_dir + ".key", self.manifest_data)
                print("[+] 提取完成！")

        except Exception as e:
            print(f"[!] 提取失败: {e}")

    def _parse_index(self, raw_index_data, count):
        entries = []
        data_base = 4 + (count * 136)
        
        for i in range(count):
            key = self.keys[i]
            raw_entry = raw_index_data[i*136 : (i+1)*136]
            dec_entry = FolCrypto.transform_index(raw_entry, key, False)
            
            name_bytes = dec_entry[:128]
            name_end = name_bytes.find(b'\x00')
            if name_end != -1: name_bytes = name_bytes[:name_end]
            
            try: name = name_bytes.decode('gbk')
            except: name = name_bytes.decode('ascii', errors='ignore')
            
            offset = struct.unpack('<I', dec_entry[128:132])[0]
            size = struct.unpack('<I', dec_entry[132:136])[0]
            
            # 偏移量修正
            if offset < data_base and offset > 0: offset += data_base
            
            entries.append({'name': name, 'offset': offset, 'size': size})
            self.manifest_data.append({'name': name.replace('/', '\\'), 'key': key, 'index': i})
        return entries

# ============================
# 主入口
# ============================
def main():
    parser = argparse.ArgumentParser(description="Pixel Software FOL Tool (Final Fixed)")
    subparsers = parser.add_subparsers(dest='command', required=True)
    
    p_unpack = subparsers.add_parser('unpack')
    p_unpack.add_argument('input_file')
    p_unpack.add_argument('-o', '--out')
    
    p_pack = subparsers.add_parser('pack')
    p_pack.add_argument('input_dir')
    p_pack.add_argument('-o', '--out')
    p_pack.add_argument('--key-file')
    
    args = parser.parse_args()
    
    if args.command == 'unpack':
        out = args.out if args.out else f"{os.path.splitext(os.path.basename(args.input_file))[0]}_fol"
        FolExtractor(args.input_file, out).extract()
        
    elif args.command == 'pack':
        out = args.out if args.out else "output.fol"
        key_file = args.key_file
        if not key_file:
            pot = args.input_dir.rstrip(os.sep) + ".key"
            if os.path.exists(pot): key_file = pot
        
        FolPacker(args.input_dir, out, key_file).pack()

if __name__ == "__main__":
    main()