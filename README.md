# 基于C/C++开发的TinyWebServer
## Introduction
这是一个小型的HTTPWeb服务器，它基于epoll的I/O多路复用和多线程实现并发，主线程负责客户的连接和数据的读写，工作线程负责解析HTTP请求。

| Part Ⅰ | Part Ⅱ | Part Ⅲ | Part Ⅳ |
| :--------: | :---------: | :---------: | :---------: | 
| [项目目的](https://github.com/ofdm/myHttpServer/blob/master/项目目的.md)|[历史版本](https://github.com/ofdm/myHttpServer/blob/master/历史版本.md)|[压力测试](https://github.com/ofdm/myHttpServer/blob/master/压力测试.md) | [遇到困难](https://github.com/ofdm/myHttpServer/blob/master/遇到困难.md)

## 流程框图

## 开发环境
- 操作系统：Ubuntu 16.04（虚拟机）
- 编辑器：Sublime + Vim
- 编译器：gcc 5.4.0
- 压测工具：[WebBench](https://github.com/ofdm/myHttpServer/blob/master/src/WebBench)

## 用法
修改http_conn.cpp中root的地址（html路径）
```
make
./myHttpServer ip port
```
## 技术点
* 使用了epoll边沿触发IO多路复用技术
* 使用了一个固定线程数的线程池
* 使用了互斥锁及条件变量用于线程控制
* 使用了状态机解析HTTP请求
* 使用了时间堆定时器剔除非活动连接客户
