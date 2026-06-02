# Windows 启动指南

本文档说明如何在 Windows 环境下编译并运行本 VFS 虚拟文件系统程序。

---

## 1. 打开终端并进入项目目录

推荐使用 PowerShell 或 Windows Terminal。

```powershell
cd d:\os_ks
```

---

## 2. 编译程序

### 方式一：使用 make

如果你的环境已经安装了 `make`，可以直接执行：

```powershell
make
```

编译成功后，项目目录下会生成：

```text
vfs.exe
```

### 方式二：直接使用 gcc

如果没有 `make`，但已经安装了 `gcc`，可以直接执行：

```powershell
gcc -std=c11 -g -O0 -o vfs.exe src/*.c
```

编译成功后，同样会生成：

```text
vfs.exe
```

---

## 3. 运行程序

运行时需要指定一个 `.dat` 文件作为虚拟磁盘文件。例如：

```powershell
.\vfs.exe filesystem.dat
```

也可以使用测试文件名：

```powershell
.\vfs.exe myfs.dat
```

说明：

- 如果该 `.dat` 文件已经存在，程序会加载已有文件系统。
- 如果该 `.dat` 文件不存在，程序会自动创建并格式化一个新的文件系统。

---

## 4. 登录用户

进入程序后，会看到命令提示符：

```text
$
```

文件操作前必须先登录。默认用户为 `usr1` 到 `usr8`，密码都是 `123`。

示例：

```text
login usr1 123
```

---

## 5. 基本操作示例

登录后可以依次输入以下命令测试文件系统功能：

```text
mkdir /demo
create /demo/hello.txt rw
open /demo/hello.txt rw
write 0 hello_world
seek 0 0
read 0 11
dir /demo
close 0
exit
```

说明：

| 命令 | 作用 |
|------|------|
| `mkdir /demo` | 创建目录 `/demo` |
| `create /demo/hello.txt rw` | 创建文件并设置读写权限 |
| `open /demo/hello.txt rw` | 以读写模式打开文件 |
| `write 0 hello_world` | 向文件描述符 0 写入内容 |
| `seek 0 0` | 将文件偏移移动到开头 |
| `read 0 11` | 从文件描述符 0 读取 11 字节 |
| `dir /demo` | 查看 `/demo` 目录内容 |
| `close 0` | 关闭文件描述符 0 |
| `exit` | 保存并退出程序 |

---

## 6. 一次完整运行流程

PowerShell 中执行：

```powershell
cd d:\os_ks
gcc -std=c11 -g -O0 -o vfs.exe src/*.c
.\vfs.exe myfs.dat
```

进入 VFS 后输入：

```text
login usr1 123
mkdir /demo
create /demo/hello.txt rw
open /demo/hello.txt rw
write 0 hello_world
seek 0 0
read 0 11
dir /demo
close 0
exit
```

---

## 7. 退出程序

在 VFS 内部输入：

```text
exit
```

程序会执行以下保存流程：

1. 刷新所有内存 inode 到磁盘。
2. 保存超级块。
3. 关闭宿主 `.dat` 文件。

因此，推荐始终使用 `exit` 正常退出，避免直接关闭终端。

---

## 8. 常见问题

### 8.1 提示 gcc 不是内部或外部命令

说明 Windows 没有正确安装或配置 GCC。可以安装 MinGW-w64、MSYS2 或 TDM-GCC，并将 `gcc.exe` 所在目录加入系统 PATH。

### 8.2 提示 make 不是内部或外部命令

说明没有安装 `make`。可以直接使用：

```powershell
gcc -std=c11 -g -O0 -o vfs.exe src/*.c
```

### 8.3 程序启动后不能创建文件

请确认已经先登录：

```text
login usr1 123
```

### 8.4 想重新初始化文件系统

进入 VFS 后输入：

```text
format
```

注意：`format` 会清空当前 `.dat` 文件系统中的所有数据。
