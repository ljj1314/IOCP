#include "stdafx.h"
#include "StreamLog.h"
#include <sstream>

#include "MainDlg.h"

namespace DEFTOOLS {
	LogFile::LogFile() :pOfsLogFile_(NULL){
		filePath_ = "";
		fileName_ = filePath_ + MakeFileName();
	}

	LogFile::LogFile(const std::string& filePath) : pOfsLogFile_(NULL){
		filePath_ = filePath;
		fileName_ = filePath_ + MakeFileName();
	}

	const std::string LogFile::MakeFileName() {
		struct tm t = tm_.GetCurrentDataTime();
		std::stringstream s;
		std::string str;
		s << t.tm_year + 1900 << "_" << t.tm_mon + 1 << "_"
			<< t.tm_mday << "_" << t.tm_hour << "_" <<
			t.tm_min << "_" << t.tm_sec << ".log";
		s >> str;
		return str;
	}
	bool LogFile::CreateFile() {
		bool bRet = false;
		try {
			pOfsLogFile_ = new std::ofstream(fileName_.c_str(), std::ios_base::app);
			if (NULL == pOfsLogFile_ || pOfsLogFile_->bad())
			{
				pOfsLogFile_ = NULL;
				std::exception e("open ofstream error");
				throw e;
			}
			tm_.SetBeginTime();
			bRet = true;
		}
		catch (const std::exception& error)
		{
			std::cerr << error.what() << std::endl;
		}

		return bRet;
	}


	bool LogFile::InitFunc() {
		return CreateFile();
	}

	LogFile::~LogFile(){
		if (NULL != pOfsLogFile_)
		{
			//pOfsLogFile_->close();
			delete pOfsLogFile_;
			pOfsLogFile_ = NULL;
		}
	}

	void LogFile::CheckFile() {
		tm_.SetEndTime();
		if ((tm_.GetDeltaTime() > DEFAULT_DELTA_TIME) ||
			GetFileSize() > DEFAULT_FILE_SIZE) {
				if (NULL != pOfsLogFile_) {
					pOfsLogFile_->close();
					delete pOfsLogFile_;
					pOfsLogFile_ = NULL;
				}
				fileName_ = filePath_ + MakeFileName();
				CreateFile();
		}
	}

	const std::string LogFile::GetCurrentTimeString()
	{
		struct tm t = tm_.GetCurrentDataTime();
		std::stringstream s;
		std::string str;
		std::string strTime;
		s << t.tm_year + 1900 << "-" << t.tm_mon + 1 << "-"
			<< t.tm_mday;
		s >> str;
		s.clear();
		s << t.tm_hour << ":" <<
			t.tm_min << ":" << t.tm_sec << "\t\t";
		s >> strTime;

		str += " ";
		str += strTime;
		return str;
	}

	std::string  LogFile::GetLevelString(const WriteLevel wl) {
		std::string levelStr;
		switch (wl)
		{
		case NORMAL_L:
			levelStr = "[normal ]\t";
			break;
		case WARNING_L:
			levelStr = "[warning]\t";
			break;
		case ERROR_L:
			levelStr = "[error  ]\t";
			break;
		case UNKNOWN_L:
			levelStr = "[unknown]\t";
			break;
		default:
			levelStr = "[???????]\t";
			break;
		}
		return levelStr;
	}

	bool LogFile::_CheckWrite()
	{
		//从配置文件中读取写入标志
		char szPath[MAX_PATH] = {0};
		GetModuleFileName(NULL,szPath,MAX_PATH);
		std::string strFileName = szPath;
		int iPos = strFileName.find_last_of("\\");
		std::string strFilePath = strFileName.substr(0,iPos + 1);
		strFilePath += "Config.ini";
		char szlog[4] = {0};
		if( GetPrivateProfileString("General","logflag","",szlog,4,strFilePath.c_str()) )
		{
			return strcmp(szlog,"0") == 0 ? false : true;
		}

		return true;
	}

	bool LogFile::WriteLog(const std::string &wrtieStr, const WriteLevel wl) {

		//if( !_CheckWrite() )
		//{
		//	return false;
		//}
		bool bCheck = CMainDlg::GetLogFlag();
		if( false == bCheck )
		{
			return false;
		}
		bool bRet = false;
		CheckFile();
		if (!pOfsLogFile_) {
			return bRet;
		}
		(*pOfsLogFile_) << GetCurrentTimeString() << GetLevelString(wl) << wrtieStr << std::endl;
		bRet = true;
		return bRet;
	}


}//namespace DEFTOOLS 