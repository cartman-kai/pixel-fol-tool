#pragma once

// 图标 ID
#define IDI_MAIN_ICON           101

// 对话框 ID
#define IDD_MAIN_DIALOG         102

// 控件 ID 定义
// 规则：IDC_控件类型_功能描述

// 解包区域 (Unpack)
#define IDC_GRP_UNPACK          1001
#define IDC_EDIT_FOL_PATH       1002 // 显示 .fol 路径
#define IDC_BTN_BROWSE_FOL      1003 // 浏览文件按钮
#define IDC_EDIT_OUT_DIR        1004 // 显示输出目录
#define IDC_BTN_BROWSE_OUT      1005 // 浏览目录按钮
#define IDC_BTN_RUN_UNPACK      1006 // 执行解包

// 打包区域 (Pack)
#define IDC_GRP_PACK            1007
#define IDC_EDIT_IN_DIR         1008 // 资源根目录
#define IDC_BTN_BROWSE_IN       1009
#define IDC_EDIT_FOL_OUT        1010 // 输出文件路径
#define IDC_BTN_BROWSE_SAVE     1011
#define IDC_BTN_RUN_PACK        1012 // 执行打包

// 公共区域
#define IDC_STATIC_STATUS       1013 // 状态文本标签
#define IDC_EDIT_LOG            1014 // 只读日志框
#define IDC_PROGRESS_BAR        1015 // 进度条

// 字符串表 ID (用于多语言)
#define IDS_APP_TITLE           2001
#define IDS_GRP_UNPACK          2002
#define IDS_GRP_PACK            2003
#define IDS_BTN_BROWSE          2004
#define IDS_BTN_UNPACK          2005
#define IDS_BTN_PACK            2006
#define IDS_READY               2007
#define IDS_ERR_NO_FOL          2008
#define IDS_ERR_NO_DIR          2009
#define IDS_SUCCESS             2010

#define IDC_STATIC_AUTHOR       1016
#define IDC_STATIC_LINK         1017
#define IDC_STATIC_DESC         1018
#define IDC_GRP_ABOUT           1019

// 字符串 ID
#define IDS_ABOUT_DESC          2011
