
# Cloud clipboard

[cloud-clipboard](https://github.com/TransparentLC/cloud-clipboard) 服务端的 c 实现

> [!TIP]
> 目前只做了文本剪贴板的功能，房间与文件传输没有实现

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





