#!/usr/bin/env python3
"""
STM32N6 Model Packager
============================

This tool creates model packages for the STM32N6 dynamic configuration system.
Supports network_rel.bin format and external JSON configuration.

Usage:
    python model_packager.py create --model network_rel.bin --config model_config.json --output model.bin
    python model_packager.py extract --package model.bin --output extracted/
    python model_packager.py validate --package model.bin
"""

import os
import sys
import json
import struct
import argparse
import binascii
import subprocess
import re
from datetime import datetime
from typing import Dict, Any, Optional

# Package format constants
PACKAGE_MAGIC = 0x314D364E  # 'N6M1' - v3.0
PACKAGE_VERSION = 0x030000   # v3.0.0
RELOCATABLE_MODEL_MAGIC = 0x4E49424E  # 'NBIN'

# Header format: 20 x uint32 (device-compatible)
# Fields:
# 0 magic
# 1 version
# 2 package_size
# 3 metadata_offset
# 4 metadata_size
# 5 model_config_offset
# 6 model_config_size
# 7 relocatable_model_offset
# 8 relocatable_model_size
# 9 extension_data_offset
# 10 extension_data_size
# 11 header_checksum (computed over bytes before this field)
# 12 model_checksum
# 13 config_checksum
# 14 package_checksum
HEADER_FMT = '<15I'
NUM_HEADER_FIELDS = 15
CHECKSUM_FIELD_COUNT = 4
PACKAGE_HEADER_SIZE = struct.calcsize(HEADER_FMT)
HEADER_CHECKSUM_OFFSET = (NUM_HEADER_FIELDS - CHECKSUM_FIELD_COUNT) * 4  # byte offset of header_checksum
PACKAGE_CHECKSUM_OFFSET = (NUM_HEADER_FIELDS - 1) * 4                    # byte offset of package_checksum

def get_stedgeai_executable() -> str:
    """Prefer stedgeai under STEDGEAI_CORE_DIR over PATH."""
    core_dir = os.environ.get('STEDGEAI_CORE_DIR')
    if core_dir:
        candidates = [
            os.path.join(core_dir, 'Utilities', 'windows', 'stedgeai.exe'),
            os.path.join(core_dir, 'Utilities', 'windows', 'stedgeai'),
            os.path.join(core_dir, 'Utilities', 'linux', 'stedgeai'),
        ]
        for candidate in candidates:
            if os.path.isfile(candidate):
                return candidate
    return 'stedgeai'


def ensure_stedgeai_toolchain_matches() -> bool:
    """Exit early if installed stedgeai does not match STEDGEAI_VARIANT."""
    check_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'check_stedgeai_toolchain.py')
    result = subprocess.run([sys.executable, check_script], check=False)
    return result.returncode == 0


def get_stedgeai_version() -> Optional[str]:
    """Get ST Edge AI Core version string from stedgeai --version."""
    stedgeai_bin = get_stedgeai_executable()
    try:
        result = subprocess.run(
            [stedgeai_bin, '--version'],
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.returncode != 0 or not result.stdout:
            return None
        first_line = result.stdout.strip().split('\n')[0]
        m = re.search(r'(v[\d]+\.[\d]+\.[\d]+(?:-[\w]+)?(?:\s+\d+)?)', first_line)
        if m:
            return m.group(1).strip()
        return None
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
        return None


class ModelPackager:
    def __init__(self):
        pass

    def calculate_crc32(self, data: bytes) -> int:
        """Calculate CRC32 checksum"""
        return binascii.crc32(data) & 0xffffffff

    def validate_relocatable_model(self, model_path: str) -> bool:
        """Validate network_rel.bin format"""
        try:
            with open(model_path, 'rb') as f:
                # Check magic number
                magic = struct.unpack('<I', f.read(4))[0]
                if magic != RELOCATABLE_MODEL_MAGIC:
                    print(f"Invalid relocatable model magic: 0x{magic:08X} (expected: 0x{RELOCATABLE_MODEL_MAGIC:08X})")
                    return False
                
                print(f"[OK] Valid relocatable model: {model_path}")
                print(f"  Magic: 0x{magic:08X}")
                
                # Get file size
                f.seek(0, 2)  # Seek to end
                size = f.tell()
                print(f"  Size: {size:,} bytes ({size/1024/1024:.2f} MB)")
                
                return True
        except Exception as e:
            print(f"Error validating model: {e}")
            return False

    def load_json_config(self, config_path: str) -> Optional[Dict[str, Any]]:
        """Load and validate JSON configuration"""
        try:
            with open(config_path, 'r', encoding='utf-8') as f:
                config = json.load(f)
            
            # Basic validation
            required_fields = ['model_info', 'input_spec', 'output_spec']
            for field in required_fields:
                if field not in config:
                    print(f"Missing required field: {field}")
                    return None
            
            print(f"[OK] Valid configuration: {config_path}")
            print(f"  Model: {config['model_info'].get('name', 'Unknown')}")
            print(f"  Version: {config['model_info'].get('version', 'Unknown')}")
            print(f"  Type: {config['model_info'].get('type', 'Unknown')}")
            
            return config
        except Exception as e:
            print(f"Error loading configuration: {e}")
            return None

    def create_package(self, model_path: str, config_path: str, output_path: str) -> bool:
        """Create model package v2.1 with external configuration"""

        if not ensure_stedgeai_toolchain_matches():
            return False
        
        # Validate inputs
        if not self.validate_relocatable_model(model_path):
            return False
        
        config = self.load_json_config(config_path)
        if not config:
            return False
        
        try:
            # Load model data
            with open(model_path, 'rb') as f:
                model_data = f.read()
            
            # Load configuration data
            with open(config_path, 'rb') as f:
                config_data = f.read()
            
            # Calculate offsets and sizes
            header_size = PACKAGE_HEADER_SIZE
            
            # Metadata (package info JSON)
            stedgeai_ver = get_stedgeai_version()
            metadata = {
                "created_at": datetime.now().isoformat(),
                "created_by": "STM32N6 Model Packager",
                "model_info": config['model_info'],
                "package_version": f"{PACKAGE_VERSION >> 16}.{(PACKAGE_VERSION >> 8) & 0xFF}.{PACKAGE_VERSION & 0xFF}",
                "stedgeai_version": stedgeai_ver if stedgeai_ver else "unknown",
            }
            metadata_data = json.dumps(metadata, indent=2, sort_keys=True).encode('utf-8')
            
            # Calculate section offsets
            current_offset = header_size
            
            metadata_offset = current_offset
            metadata_size = len(metadata_data)
            current_offset += metadata_size
            
            model_config_offset = current_offset
            model_config_size = len(config_data)
            current_offset += model_config_size
            
            # Align model data to 1KB boundary
            model_data_offset = ((current_offset + 1023) // 1024) * 1024
            model_data_size = len(model_data)
            current_offset = model_data_offset + model_data_size
            
            extension_data_offset = current_offset
            extension_data_size = 0  # No extension data for now
            
            total_package_size = current_offset
            
            # Calculate checksums
            model_checksum = self.calculate_crc32(model_data)
            config_checksum = self.calculate_crc32(config_data)
            
            # Create package header (header_checksum and package_checksum set to 0 for now)
            header = struct.pack(
                HEADER_FMT,
                PACKAGE_MAGIC,                   # magic
                PACKAGE_VERSION,                 # version
                total_package_size,              # package_size
                metadata_offset,                 # metadata_offset
                metadata_size,                   # metadata_size
                model_config_offset,             # model_config_offset
                model_config_size,               # model_config_size
                model_data_offset,               # relocatable_model_offset
                model_data_size,                 # relocatable_model_size
                extension_data_offset,           # extension_data_offset
                extension_data_size,             # extension_data_size
                0,                               # header_checksum (calculated later)
                model_checksum,                  # model_checksum
                config_checksum,                 # config_checksum
                0                                # package_checksum (calculated later)
            )
            
            # Calculate header checksum (exclude checksum fields region)
            header_for_checksum = header[:HEADER_CHECKSUM_OFFSET]
            header_checksum = self.calculate_crc32(header_for_checksum)
            
            # Update header with header checksum at dynamic offset
            header = header[:HEADER_CHECKSUM_OFFSET] + struct.pack('<I', header_checksum) + header[HEADER_CHECKSUM_OFFSET + 4:]
            
            # Write package
            with open(output_path, 'wb') as f:
                # Write header
                f.write(header)
                
                # Write metadata
                f.write(metadata_data)
                
                # Write configuration
                f.write(config_data)
                
                # Pad to model data offset
                current_pos = f.tell()
                padding_size = model_data_offset - current_pos
                if padding_size > 0:
                    f.write(b'\x00' * padding_size)
                
                # Write model data
                f.write(model_data)
            
            # Calculate and update package checksum
            # First, ensure package checksum field is 0 for calculation
            with open(output_path, 'r+b') as f:
                f.seek(PACKAGE_CHECKSUM_OFFSET)
                f.write(struct.pack('<I', 0))
            
            # Now calculate checksum of the entire package
            with open(output_path, 'rb') as f:
                package_data = f.read()
            
            package_checksum = self.calculate_crc32(package_data)
            
            # Update package checksum in header
            with open(output_path, 'r+b') as f:
                f.seek(PACKAGE_CHECKSUM_OFFSET)
                f.write(struct.pack('<I', package_checksum))
            
            # Print package information
            print(f"[OK] Package created successfully: {output_path}")
            print(f"  Config: {config_path}")
            print(f"  Total size: {total_package_size:,} bytes ({total_package_size/1024/1024:.2f} MB)")
            print(f"  Model size: {model_data_size:,} bytes ({model_data_size/1024/1024:.2f} MB)")
            print(f"  Config size: {model_config_size:,} bytes")
            print(f"  header_checksum: 0x{header_checksum:08X}")
            print(f"  Model checksum: 0x{model_checksum:08X}")
            print(f"  Config checksum: 0x{config_checksum:08X}")
            print(f"  Package checksum: 0x{package_checksum:08X}")
            
            return True
            
        except Exception as e:
            print(f"Error creating package: {e}")
            import traceback
            traceback.print_exc()
            return False

    def validate_package(self, package_path: str) -> bool:
        """Validate model package"""
        try:
            with open(package_path, 'rb') as f:
                # Read header
                header_data = f.read(PACKAGE_HEADER_SIZE)
                if len(header_data) < PACKAGE_HEADER_SIZE:
                    print("Invalid package: Header too small")
                    return False
                
                header = struct.unpack(HEADER_FMT, header_data)
                (magic, version, package_size,
                 metadata_offset, metadata_size,
                 model_config_offset, model_config_size,
                 relocatable_model_offset, relocatable_model_size,
                 extension_data_offset, extension_data_size,
                 header_checksum, model_checksum, config_checksum, package_checksum) = header
                
                # Validate magic and version
                if magic != PACKAGE_MAGIC:
                    print(f"Invalid magic: 0x{magic:08X} (expected: 0x{PACKAGE_MAGIC:08X})")
                    return False
                
                if version != PACKAGE_VERSION:
                    print(f"Unsupported version: 0x{version:08X} (expected: 0x{PACKAGE_VERSION:08X})")
                    return False
                
                # Validate header checksum
                header_for_checksum = header_data[:HEADER_CHECKSUM_OFFSET]
                calculated_header_checksum = self.calculate_crc32(header_for_checksum)
                if calculated_header_checksum != header_checksum:
                    print(f"Header checksum mismatch: 0x{calculated_header_checksum:08X} vs 0x{header_checksum:08X}")
                    return False
                
                # Validate model checksum
                f.seek(relocatable_model_offset)
                model_data = f.read(relocatable_model_size)
                calculated_model_checksum = self.calculate_crc32(model_data)
                if calculated_model_checksum != model_checksum:
                    print(f"Model checksum mismatch: 0x{calculated_model_checksum:08X} vs 0x{model_checksum:08X}")
                    return False
                
                # Validate package checksum
                f.seek(0)
                all_data = bytearray(f.read())
                # Zero out the package checksum field at dynamic offset
                all_data[PACKAGE_CHECKSUM_OFFSET:PACKAGE_CHECKSUM_OFFSET+4] = b'\x00\x00\x00\x00'
                calculated_package_checksum = self.calculate_crc32(bytes(all_data))
                if calculated_package_checksum != package_checksum:
                    print(f"Package checksum mismatch: 0x{calculated_package_checksum:08X} vs 0x{package_checksum:08X}")
                    return False
                
                # Load and display metadata
                f.seek(metadata_offset)
                metadata_data = f.read(metadata_size)
                try:
                    metadata = json.loads(metadata_data.decode('utf-8'))
                    model_info = metadata.get('model_info', {})
                except Exception:
                    model_info = {}
                    metadata = {}
                
                print(f"[OK] Valid package: {package_path}")
                print(f"  Version: {version >> 16}.{(version >> 8) & 0xFF}.{version & 0xFF}")
                print(f"  Model: {model_info.get('name', 'Unknown')}")
                print(f"  Framework: {model_info.get('framework', 'Unknown')}")
                print(f"  StedgeAI: {metadata.get('stedgeai_version', 'unknown')}")
                print(f"  Size: {package_size:,} bytes ({package_size/1024/1024:.2f} MB)")
                print(f"  Model size: {relocatable_model_size:,} bytes ({relocatable_model_size/1024/1024:.2f} MB)")
                print(f"  Config size: {model_config_size:,} bytes")
                
                return True
                
        except Exception as e:
            print(f"Error validating package: {e}")
            return False

    def extract_package(self, package_path: str, output_dir: str) -> bool:
        """Extract model package contents"""
        try:
            os.makedirs(output_dir, exist_ok=True)
            
            with open(package_path, 'rb') as f:
                # Read header
                header_data = f.read(PACKAGE_HEADER_SIZE)
                header = struct.unpack(HEADER_FMT, header_data)
                (magic, version, package_size,
                 metadata_offset, metadata_size,
                 model_config_offset, model_config_size,
                 relocatable_model_offset, relocatable_model_size,
                 extension_data_offset, extension_data_size,
                 header_checksum, model_checksum, config_checksum, package_checksum) = header
                
                # Extract metadata
                metadata_obj = {}
                if metadata_size > 0:
                    f.seek(metadata_offset)
                    metadata_data = f.read(metadata_size)
                    with open(os.path.join(output_dir, 'metadata.json'), 'wb') as out_f:
                        out_f.write(metadata_data)
                    try:
                        metadata_obj = json.loads(metadata_data.decode('utf-8'))
                    except Exception:
                        pass
                
                # Extract configuration
                if model_config_size > 0:
                    f.seek(model_config_offset)
                    config_data = f.read(model_config_size)
                    with open(os.path.join(output_dir, 'model_config.json'), 'wb') as out_f:
                        out_f.write(config_data)
                
                # Extract model data
                f.seek(relocatable_model_offset)
                model_data = f.read(relocatable_model_size)
                with open(os.path.join(output_dir, 'network_rel.bin'), 'wb') as out_f:
                    out_f.write(model_data)
                
                # Save package information
                package_info = {
                    "package_format": "STM32N6 Model Package",
                    "magic": f"0x{magic:08X}",
                    "version": f"{version >> 16}.{(version >> 8) & 0xFF}.{version & 0xFF}",
                    "stedgeai_version": metadata_obj.get("stedgeai_version", "unknown"),
                    "package_size": package_size,
                    "sections": {
                        "metadata": {"offset": metadata_offset, "size": metadata_size},
                        "model_config": {"offset": model_config_offset, "size": model_config_size},
                        "relocatable_model": {"offset": relocatable_model_offset, "size": relocatable_model_size},
                        "extension_data": {"offset": extension_data_offset, "size": extension_data_size}
                    },
                    "checksums": {
                        "header": f"0x{header_checksum:08X}",
                        "model": f"0x{model_checksum:08X}",
                        "config": f"0x{config_checksum:08X}",
                        "package": f"0x{package_checksum:08X}"
                    }
                }
                
                with open(os.path.join(output_dir, 'package_info.json'), 'w', encoding='utf-8') as f:
                    json.dump(package_info, f, indent=2)
                
                print(f"[OK] Package extracted to: {output_dir}")
                print(f"  Files created:")
                for file_name in os.listdir(output_dir):
                    file_path = os.path.join(output_dir, file_name)
                    file_size = os.path.getsize(file_path)
                    print(f"    {file_name}: {file_size:,} bytes")
                
                return True
                
        except Exception as e:
            print(f"Error extracting package: {e}")
            return False


def main():
    parser = argparse.ArgumentParser(description='STM32N6 Model Packager')
    subparsers = parser.add_subparsers(dest='command', help='Available commands')
    
    # Create package command
    create_parser = subparsers.add_parser('create', help='Create model package')
    create_parser.add_argument('--model', required=True, help='Path to network_rel.bin file')
    create_parser.add_argument('--config', required=True, help='Path to model configuration JSON')
    create_parser.add_argument('--output', required=True, help='Output package path (.bin)')
    
    # Extract package command
    extract_parser = subparsers.add_parser('extract', help='Extract package contents')
    extract_parser.add_argument('--package', required=True, help='Path to model package')
    extract_parser.add_argument('--output', required=True, help='Output directory')
    
    # Validate package command
    validate_parser = subparsers.add_parser('validate', help='Validate package')
    validate_parser.add_argument('--package', required=True, help='Path to model package')
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return 1
    
    packager = ModelPackager()
    
    if args.command == 'create':
        success = packager.create_package(args.model, args.config, args.output)
    elif args.command == 'extract':
        success = packager.extract_package(args.package, args.output)
    elif args.command == 'validate':
        success = packager.validate_package(args.package)
    else:
        parser.print_help()
        return 1
    
    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
