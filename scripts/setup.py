#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os
import sys
# Windows cmd 强制 UTF-8 输出
if sys.platform == "win32":
    os.system("chcp 65001 >nul 2>&1")
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
"""
VERUM 第三方库自动配置脚本
用法:
    python scripts/setup.py           # 下载并配置所有依赖
    python scripts/setup.py --list    # 列出所有依赖状态
    python scripts/setup.py --force   # 强制重新下载（覆盖已存在的库）

依赖库：
    1. nlohmann/json  v3.11.3   - 单头文件 JSON 库
    2. WebView2 SDK   1.0.3351  - 微软 WebView2 NuGet 包
    3. WIL            1.0.245   - Windows Implementation Library
    4. nfd-extended   1.2.1     - 原生文件对话框（含源码）
"""

import argparse
import sys
import shutil
import zipfile
import urllib.request
import urllib.error
from pathlib import Path

# ── 路径常量 ──────────────────────────────────────────────────────────────────
ROOT        = Path(__file__).parent.parent.resolve()
THIRD_PARTY = ROOT / "third_party"

# ── 依赖定义 ──────────────────────────────────────────────────────────────────
DEPS = [
    {
        "name":    "nlohmann/json",
        "version": "3.11.3",
        "type":    "single_header",
        "url":     "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp",
        "dest":    THIRD_PARTY / "nlohmann" / "json.hpp",
        "check":   THIRD_PARTY / "nlohmann" / "json.hpp",
    },
    {
        "name":    "Microsoft WebView2 SDK",
        "version": "1.0.3351.48",
        "type":    "nuget_zip",
        # NuGet 包直接以 zip 格式下载
        "url":     "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.3351.48",
        "dest":    THIRD_PARTY / "_downloads" / "webview2.zip",
        "extract": THIRD_PARTY / "_downloads" / "webview2_pkg",
        # 实际路径：build/native/include/ + build/native/x64/（含 WebView2LoaderStatic.lib）
        "copy_map": {
            "build/native/include": THIRD_PARTY / "webview2" / "include",
            "build/native/x64":     THIRD_PARTY / "webview2" / "lib" / "x64",
        },
        "check": THIRD_PARTY / "webview2" / "include" / "WebView2.h",
    },
    # WIL 已移除：使用标准 Win32 CoTaskMemFree 替代 wil::unique_cotaskmem_string
    # WebView2 SDK 静态 lib 已包含所有必要功能
    {
        "name":    "nativefiledialog-extended",
        "version": "1.2.1",
        "type":    "github_zip",
        "url":     "https://github.com/btzy/nativefiledialog-extended/archive/refs/tags/v1.2.1.zip",
        "dest":    THIRD_PARTY / "_downloads" / "nfd.zip",
        "extract": THIRD_PARTY / "_downloads" / "nfd_src",
        # 将解压的子目录重命名到目标位置
        "rename":  {
            "src_subdir": "nativefiledialog-extended-1.2.1",
            "dst":        THIRD_PARTY / "nfd",
        },
        "check": THIRD_PARTY / "nfd" / "CMakeLists.txt",
    },
]

# ── 工具函数 ──────────────────────────────────────────────────────────────────

def print_ok(msg: str):  print(f"  \033[32m✓\033[0m {msg}")
def print_skip(msg: str):print(f"  \033[33m-\033[0m {msg}")
def print_err(msg: str): print(f"  \033[31m✗\033[0m {msg}", file=sys.stderr)
def print_info(msg: str):print(f"  \033[36m→\033[0m {msg}")


def download(url: str, dest: Path, desc: str = ""):
    """下载文件，显示进度"""
    dest.parent.mkdir(parents=True, exist_ok=True)
    print_info(f"下载 {desc or url}")

    def reporthook(count, block_size, total_size):
        if total_size > 0:
            pct = min(count * block_size * 100 // total_size, 100)
            print(f"\r    {pct}%", end="", flush=True)

    try:
        urllib.request.urlretrieve(url, dest, reporthook)
        print()  # 换行
        return True
    except urllib.error.URLError as e:
        print()
        print_err(f"下载失败: {e}")
        return False


def extract_zip(zip_path: Path, extract_to: Path):
    """解压 zip 文件"""
    extract_to.mkdir(parents=True, exist_ok=True)
    print_info(f"解压 → {extract_to.name}/")
    with zipfile.ZipFile(zip_path, 'r') as zf:
        zf.extractall(extract_to)


def copy_dir(src: Path, dst: Path):
    """复制目录（覆盖已存在）"""
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)


# ── 各类型安装器 ──────────────────────────────────────────────────────────────

def install_single_header(dep: dict, force: bool) -> bool:
    check: Path = dep["check"]
    if check.exists() and not force:
        print_skip(f"{dep['name']} 已存在，跳过")
        return True

    dest: Path = dep["dest"]
    if not download(dep["url"], dest, dep["name"]):
        return False
    print_ok(f"{dep['name']} v{dep['version']} 安装完成")
    return True


def install_nuget_zip(dep: dict, force: bool) -> bool:
    check: Path = dep["check"]
    if check.exists() and not force:
        print_skip(f"{dep['name']} 已存在，跳过")
        return True

    dest: Path   = dep["dest"]
    extract: Path = dep["extract"]

    if not dest.exists() or force:
        if not download(dep["url"], dest, dep["name"]):
            return False

    if extract.exists() and force:
        shutil.rmtree(extract)
    if not extract.exists():
        extract_zip(dest, extract)

    # 复制需要的文件
    for src_rel, dst_abs in dep["copy_map"].items():
        src = extract / src_rel
        if src.exists():
            print_info(f"复制 {src_rel} → {dst_abs.relative_to(THIRD_PARTY)}")
            copy_dir(src, dst_abs)
        else:
            print_err(f"未找到: {src}")
            return False

    print_ok(f"{dep['name']} v{dep['version']} 安装完成")
    return True


def install_github_zip(dep: dict, force: bool) -> bool:
    check: Path = dep["check"]
    if check.exists() and not force:
        print_skip(f"{dep['name']} 已存在，跳过")
        return True

    dest: Path    = dep["dest"]
    extract: Path = dep["extract"]

    if not dest.exists() or force:
        if not download(dep["url"], dest, dep["name"]):
            return False

    if extract.exists() and force:
        shutil.rmtree(extract)
    if not extract.exists():
        extract_zip(dest, extract)

    # 重命名/移动子目录
    rename = dep.get("rename")
    if rename:
        src_subdir = extract / rename["src_subdir"]
        dst        = rename["dst"]
        if dst.exists():
            shutil.rmtree(dst)
        if src_subdir.exists():
            shutil.copytree(src_subdir, dst)
            # 删除 .git 目录（裁剪）
            git_dir = dst / ".git"
            if git_dir.exists():
                shutil.rmtree(git_dir)
            print_info("已移除 .git 目录（裁剪）")
        else:
            print_err(f"未找到解压子目录: {src_subdir}")
            return False

    print_ok(f"{dep['name']} v{dep['version']} 安装完成")
    return True


# ── 主入口 ────────────────────────────────────────────────────────────────────

INSTALLERS = {
    "single_header": install_single_header,
    "nuget_zip":     install_nuget_zip,
    "github_zip":    install_github_zip,
}


def list_deps():
    print("\n依赖状态：")
    for dep in DEPS:
        check: Path = dep["check"]
        status = "\033[32m已安装\033[0m" if check.exists() else "\033[31m未安装\033[0m"
        print(f"  [{status}] {dep['name']} v{dep['version']}")
    print()


def setup(force: bool = False):
    print(f"\n[SETUP] 配置 VERUM 第三方依赖 → {THIRD_PARTY}\n")
    THIRD_PARTY.mkdir(exist_ok=True)

    success = 0
    for dep in DEPS:
        print(f"\n[{dep['name']}] v{dep['version']}")
        installer = INSTALLERS.get(dep["type"])
        if installer and installer(dep, force):
            success += 1
        else:
            print_err(f"安装失败: {dep['name']}")

    print(f"\n[SETUP] 完成 {success}/{len(DEPS)} 个依赖库\n")

    if success == len(DEPS):
        print("✅ 所有依赖已就绪，可以运行 CMake 构建：")
        print("   python scripts/build.py --cpp-only\n")
    else:
        print("⚠️  部分依赖安装失败，请检查网络连接后重试\n")
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description="VERUM 第三方库配置脚本")
    parser.add_argument("--list",  action="store_true", help="列出依赖状态")
    parser.add_argument("--force", action="store_true", help="强制重新下载")
    args = parser.parse_args()

    if args.list:
        list_deps()
    else:
        setup(force=args.force)


if __name__ == "__main__":
    main()
