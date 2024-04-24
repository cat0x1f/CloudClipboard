
# Cloud clipboard

[cloud-clipboard](https://github.com/TransparentLC/cloud-clipboard) 服务端的 c 实现

> [!TIP]
> 目前只做了文本剪贴板和普通文件上传，房间与图片等内容没有实现

一些特殊的处理(主要目的是尽量流畅无影响地跑在资源紧张的路由器上)：
1. 只保留最近一次上传的文件
2. 只保留最近 50 次操作
3. 退出时清空全部内容
4. Windows 下文件缓存目录为执行目录，其他平台为 `/tmp/cloud_clipboard`

测试版下载地址: [nightly.link](https://nightly.link/xfangfang/cloud-clipboard/workflows/c/c)

### 开发

1. 将静态文件夹放置在此目录
2. 编译
```shell
cmake -B build
cmake --build build
```

### 额外

通过使用 `zig cc` 很容易就可以交叉编译到其他平台:

```shell
# 示例：交叉编译到 mipsel 平台 (PACKAGE_FILE=ON 会将静态文件打包到可执行文件中)
cmake -B build -DZIG_TARGET=mipsel-linux-musl -DPACKAGE_FILE=ON
cmake --build build
```

### todo

- [x] 文件
- [ ] 持久化储存
- [ ] 删除
- [ ] 加载配置文件
- [ ] 房间
- [ ] 图片
- [ ] 多线程上传



