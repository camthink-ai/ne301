#!/usr/bin/env python3
"""
Generate version.h from version.mk
Cross-platform compatible version header generator
"""

import os
import sys
import re
import subprocess
from datetime import datetime
from pathlib import Path

def get_git_info():
    """Get git commit and branch info"""
    git_commit = "unknown"
    git_branch = "unknown"
    git_dirty = ""
    
    try:
        git_commit = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
    except:
        pass
    
    try:
        git_branch = subprocess.check_output(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
    except:
        pass
    
    try:
        result = subprocess.run(
            ["git", "diff", "--quiet"],
            stderr=subprocess.DEVNULL
        )
        if result.returncode != 0:
            git_dirty = "-dirty"
    except:
        pass
    
    return git_commit, git_branch, git_dirty

def get_git_commit_count():
    """Get git commit count for auto BUILD number"""
    try:
        count = subprocess.check_output(
            ["git", "rev-list", "--count", "HEAD"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
        return int(count)
    except:
        return 0

def parse_version_mk(version_mk_path):
    """Parse version.mk to extract version numbers and component versions"""
    version = {
        'major': 0,
        'minor': 0,
        'patch': 0,
        'build': None,  # None means auto-generate
        'suffix': '',   # Optional version suffix
        'components': {}  # Component versions
    }
    
    if not os.path.exists(version_mk_path):
        print(f"Warning: {version_mk_path} not found, using defaults")
        return version
    
    with open(version_mk_path, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Parse main VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_SUFFIX
    major_match = re.search(r'VERSION_MAJOR\s*:?=\s*(\d+)', content)
    minor_match = re.search(r'VERSION_MINOR\s*:?=\s*(\d+)', content)
    patch_match = re.search(r'VERSION_PATCH\s*:?=\s*(\d+)', content)
    build_match = re.search(r'VERSION_BUILD\s*:?=\s*(\d+)', content)
    suffix_match = re.search(r'VERSION_SUFFIX\s*:?=\s*(\S+)', content, re.MULTILINE)
    # Exclude comment markers - if suffix starts with #, it means the value is empty
    if suffix_match and suffix_match.group(1).startswith('#'):
        suffix_match = None
    
    if major_match:
        version['major'] = int(major_match.group(1))
    if minor_match:
        version['minor'] = int(minor_match.group(1))
    if patch_match:
        version['patch'] = int(patch_match.group(1))
    if build_match:
        version['build'] = int(build_match.group(1))
    if suffix_match:
        version['suffix'] = suffix_match.group(1).strip()
    
    # Parse component versions (FSBL_VERSION, APP_VERSION, etc.)
    # These are calculated variables in Makefile, we need to extract effective values
    components = ['FSBL', 'APP', 'WEB', 'MODEL', 'WAKECORE']
    for comp in components:
        # Try to extract COMP_VERSION := value
        comp_version_match = re.search(rf'{comp}_VERSION\s*:?=\s*(.+?)(?:\n|$)', content)
        comp_suffix_match = re.search(rf'{comp}_EFFECTIVE_SUFFIX\s*:?=\s*(.+?)(?:\n|$)', content)

        if comp_version_match:
            comp_ver_str = comp_version_match.group(1).strip()
            # Remove Make functions and evaluate
            # For now, store as string
            version['components'][comp.lower()] = {
                'version_str': comp_ver_str,
                'suffix': ''
            }

    # Parse expected firmware versions
    fsbl_match = re.search(r'EXPECTED_FSBL_VERSION\s*:?=\s*(\S+)', content)
    if fsbl_match:
        version['expected_fsbl'] = fsbl_match.group(1).strip()
    wifi_match = re.search(r'EXPECTED_WIFI_VERSION\s*:?=\s*(\S+)', content)
    if wifi_match:
        version['expected_wifi'] = wifi_match.group(1).strip()

    return version

def write_if_changed(output_path, content):
    """Write content to file only if it differs from existing content.
    Returns True if file was updated, False if unchanged.
    This preserves file timestamps when nothing changed, avoiding
    unnecessary rebuilds in Make-based incremental builds.
    """
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    if os.path.exists(output_path):
        with open(output_path, 'r', encoding='utf-8') as f:
            if f.read() == content:
                return False  # unchanged — skip write

    with open(output_path, 'w', newline='\n', encoding='utf-8') as f:
        f.write(content)
    return True  # updated

def generate_version_header(output_path, version, build_override=None):
    """Generate version.h file"""
    git_commit, git_branch, git_dirty = get_git_info()
    
    # Determine BUILD number
    if build_override is not None:
        build = build_override
    elif version['build'] is not None:
        build = version['build']
    else:
        build = get_git_commit_count()
    
    major = version['major']
    minor = version['minor']
    patch = version['patch']
    suffix = version.get('suffix', '')
    
    now = datetime.now()
    build_date = now.strftime("%Y-%m-%d")
    build_time = now.strftime("%H:%M:%S")
    
    # Build version string with suffix (if any)
    version_string = f"{major}.{minor}.{patch}.{build}"
    if suffix:
        version_string = f"{version_string}_{suffix}"

    # Expected firmware versions (from version.mk, with --fsbl-version as fallback)
    expected_fsbl = version.get('expected_fsbl', '') or version.get('fsbl_version', '') or version_string
    expected_wifi = version.get('expected_wifi', '') or '0.0.0.0'

    header_content = f'''/**
 * @file version.h
 * @brief Auto-generated version information (DO NOT EDIT)
 * @generated {build_date} {build_time}
 */

#ifndef VERSION_H
#define VERSION_H

/* ==================== Main Version (APP) ==================== */
#define FW_VERSION_MAJOR    {major}
#define FW_VERSION_MINOR    {minor}
#define FW_VERSION_PATCH    {patch}
#define FW_VERSION_BUILD    {build}

/* Version string: "MAJOR.MINOR.PATCH.BUILD[_SUFFIX]" */
#define FW_VERSION_STRING   "{version_string}"

/* Packed version for numeric comparison: 0xMMmmPPBB */
#define FW_VERSION_U32      (({major}U << 24) | ({minor}U << 16) | ({patch}U << 8) | {build & 0xFF}U)


/* ==================== Build Information ==================== */
#define FW_BUILD_DATE       "{build_date}"
#define FW_BUILD_TIME       "{build_time}"
#define FW_GIT_COMMIT       "{git_commit}{git_dirty}"
#define FW_GIT_BRANCH       "{git_branch}"

/* ==================== Version Macros ==================== */
/* Compare two version numbers */
#define FW_VERSION_MAKE(major, minor, patch, build) \\
    (((major) << 24) | ((minor) << 16) | ((patch) << 8) | (build))

#define FW_VERSION_AT_LEAST(major, minor, patch) \\
    (FW_VERSION_U32 >= FW_VERSION_MAKE(major, minor, patch, 0))

/* ==================== Expected Firmware Versions ==================== */
/* Definitions in version.mk; generated here so the C code can reference them. */
#define EXPECTED_FSBL_VERSION_STRING    "{expected_fsbl}"
#define EXPECTED_WIFI_VERSION_STRING    "{expected_wifi}"

#endif /* VERSION_H */
'''
    
    # Only write if content changed (avoids unnecessary rebuilds)
    updated = write_if_changed(output_path, header_content)
    if updated:
        print(f"Version header generated: {version_string}")
    else:
        print(f"Version header unchanged: {version_string}")
    print(f"  Output: {output_path}")
    print(f"  Git: {git_commit}{git_dirty} ({git_branch})")
    
    return 0

def generate_fsbl_version_header(output_path, version_string):
    """Generate fsbl_version.h file"""
    header_content = f'''\
#ifndef FSBL_VERSION_H
#define FSBL_VERSION_H

#define FSBL_VERSION_STRING "{version_string}"
#endif
'''
    if write_if_changed(output_path, header_content):
        print(f"  Output: {output_path}")
    return 0

def generate_stedgeai_version_header(output_path, version_string):
    """Generate stedgeai_version.h file"""
    header_content = f'''\
#ifndef STEDGEAI_VERSION_H
#define STEDGEAI_VERSION_H

#define MODEL_STEDGEAI_VERSION_SUPPORTED "{version_string}"
#endif
'''
    if write_if_changed(output_path, header_content):
        print(f"  STEdgeAI: {version_string} -> {output_path}")
    return 0

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='Generate version.h from version.mk')
    parser.add_argument('-o', '--output', default='Custom/Common/Inc/version.h',
                        help='Output header file path')
    parser.add_argument('-i', '--input', default='version.mk',
                        help='Input version.mk file path')
    parser.add_argument('-b', '--build', type=int, default=None,
                        help='Override BUILD number')
    parser.add_argument('--fsbl-version', default=None,
                        help='FSBL version string (from Makefile)')
    parser.add_argument('--fsbl-output', default='FSBL/Core/Inc/fsbl_version.h',
                        help='Output FSBL version header file path')
    parser.add_argument('--stedgeai-version', default=None,
                        help='STEdgeAI version string (from Makefile)')
    parser.add_argument('--stedgeai-output', default='Custom/Hal/stedgeai_version.h',
                        help='Output STEdgeAI version header file path')
    
    args = parser.parse_args()
    
    # Get project root (script is in Script/ directory)
    script_dir = Path(__file__).parent.absolute()
    project_root = script_dir.parent
    
    version_mk_path = project_root / args.input
    output_path = project_root / args.output
    fsbl_output_path = project_root / args.fsbl_output
    stedgeai_output_path = project_root / args.stedgeai_output

    version = parse_version_mk(str(version_mk_path))

    # Add WakeCore version from command line

    if args.fsbl_version:
        version['fsbl_version'] = args.fsbl_version

    generate_fsbl_version_header(str(fsbl_output_path), version['fsbl_version'])

    if args.stedgeai_version:
        generate_stedgeai_version_header(str(stedgeai_output_path), args.stedgeai_version)

    return generate_version_header(str(output_path), version, args.build)

if __name__ == '__main__':
    sys.exit(main())

