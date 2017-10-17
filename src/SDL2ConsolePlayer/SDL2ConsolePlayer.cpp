// SDL2ConsolePlayer.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/message_loop.h"

//#include "SDL2Player.h"


int _tmain(int argc, _TCHAR* argv[])
{
	// 初始化程序环境
	base::AtExitManager at_exit_manager;	// 程序退出管理器，必须在入口函数声明
	base::MessageLoop message_loop;			// 程序消息循环，必须在入口函数声明

	// 托管命令行参数
	LPTSTR commandline = GetCommandLine();
	CommandLine cmdline = CommandLine::FromString(commandline);
	cmdline.Init(argc, nullptr);

	// 接下来可以开展核心业务
	//SDL2Player player;
	//int errCode = player.Init("rtmp://127.0.0.1:1935/live/1");

	// 首先解码视频

	// 然后渲染播放，窗口可以放大


	message_loop.Run();

	return 0;
}

