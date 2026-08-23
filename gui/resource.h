#pragma once

// 静态文本默认 ID（rc.exe 不支持负值宏，需用 RC_INVOKED 屏蔽）
#ifndef RC_INVOKED
#define IDC_STATIC              -1
#endif

// 图标 ID
#define IDI_MAIN_ICON           101

// 控件 ID 定义（主窗口手工创建）
// 规则：IDC_控件类型_功能描述

// 解包页
#define IDC_EDIT_FOL_PATH       1001 // .fol 路径（可手动输入/拖入）
#define IDC_BTN_BROWSE_FOL      1002 // 浏览文件按钮
#define IDC_EDIT_OUT_DIR        1003 // 输出目录（可手动输入）
#define IDC_BTN_BROWSE_OUT      1004 // 浏览目录按钮
#define IDC_BTN_RUN_UNPACK      1005 // 执行解包
#define IDC_LIST_PREVIEW        1006 // 归档内容预览

// 打包页
#define IDC_EDIT_IN_DIR         1011 // 资源根目录（可手动输入/拖入）
#define IDC_BTN_BROWSE_IN       1012
#define IDC_EDIT_FOL_OUT        1013 // 输出文件路径（可手动输入）
#define IDC_BTN_BROWSE_SAVE     1014
#define IDC_BTN_RUN_PACK        1015 // 执行打包

// 公共区域
#define IDC_TAB_MAIN            1021 // 解包/打包切换
#define IDC_EDIT_LOG            1022 // 只读彩色日志框
#define IDC_PROGRESS_BAR        1023 // 进度条
#define IDC_STATIC_STATUS       1024 // 状态文本标签
#define IDC_STATIC_ABOUT        1025 // 关于页信息

// 字符串表 ID（用于多语言）
#define IDS_APP_TITLE           2001
#define IDS_TAB_UNPACK          2002
#define IDS_TAB_PACK            2003
#define IDS_LBL_FOL_FILE        2004
#define IDS_LBL_OUT_DIR         2005
#define IDS_LBL_IN_DIR          2006
#define IDS_LBL_SAVE_TO         2007
#define IDS_BTN_BROWSE          2008
#define IDS_BTN_UNPACK          2009
#define IDS_BTN_PACK            2010
#define IDS_LBL_LOG             2011
#define IDS_COL_PATH            2012
#define IDS_COL_SIZE            2013
#define IDS_ST_NO_FILE          2014
#define IDS_ST_FILES            2015
#define IDS_READY               2016
#define IDS_ERR_NO_FOL          2017
#define IDS_ERR_NO_DIR          2018
#define IDS_SUCCESS             2019
#define IDS_ERR_BUSY            2020
#define IDS_TAB_ABOUT           2021
#define IDS_ABOUT_VERSION       2022
#define IDS_ABOUT_AUTHOR        2023
