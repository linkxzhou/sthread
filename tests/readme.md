### 1. 头文件
(1)mt_public.h 公用的宏定义
(2)mt_utils.h 公用的函数和类
	CPP_TAILQ 封装系统的TAILQ宏定义
	Util 公共类（获取系统时间，sleep等）
	UtilsPtrPool 对象池
	Any 对象转换类（必须继承Any）

(3)


### 2.执行流程
(1) tcp_sendrecv

tcp_sendrecv -> Frame::connect -> 
					|
				调用自定义的connect -> Frame::EventerSchedule


### 3.github地址
https://github.com/linkxzhou/mthread
https://github.com/linkxzhou/OpenCodeView
https://github.com/linkxzhou/tstl_librarys