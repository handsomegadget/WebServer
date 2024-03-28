# Linux高性能服务器开发
## 第一版
先使用游双书上的有限状态机http解析器、半同步/半反应堆线程池、epoll I/O多路复用函数实现一个最基础的HTTP服务器，能显示主页

## 第二版
测试性能，制定提高性能的方案（如EPOLLET）
增加数据库访问（或可引入redis），实现用户注册/登录。需注意安全性问题，如SQL注入

## 第三版
增设异步写日志

## 第四版
增设定时器，用最小堆实现

## 第五版
增加网页功能，能实现文件传输

## 最终
尝试微调Linux内核参数
