
# Cloud clipboard

[cloud-clipboard](https://github.com/TransparentLC/cloud-clipboard) 服务端的 c 实现

> [!TIP]
> 目前实现了文本剪贴板和文件上传，房间与密码等内容没有实现

文件缓存目录为操作系统临时目录, 对 Windows 来说是 `C:\Users\用户名\AppData\Local\Temp\cloud-clipboard`, 其他系统是 `/tmp/cloud-clipboard`

测试版下载地址: [nightly.link](https://nightly.link/xfangfang/cloud-clipboard/workflows/c/c)

### 开发

1. 将静态文件夹放置在此目录
2. 编译
```shell
cmake -B build
cmake --build build
```

### 额外

交叉编译到其他平台:

```shell
# 示例：交叉编译到 mipsel 平台 (PACKAGE_FILE=ON 会将静态文件打包到可执行文件中)
cmake -B build -DZIG_TARGET=mipsel-linux-musl -DPACKAGE_FILE=ON
cmake --build build
```

### todo

- [x] 文件
- [x] 持久化储存
- [x] 删除
- [x] 加载配置文件
- [x] 图片
- [x] 视频
- [ ] 房间
- [ ] 密码
- [ ] https
- [ ] 多线程上传
- [ ] 显示连接设备
