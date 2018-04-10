#pragma once

#include <time.h> 
#include <string>
#include <fstream>
#include <iostream>
#include <string>


namespace DEFTOOLS {
#define DEFAULT_DELTA_TIME			(1*60*24)	//日志切换时间
#define DEFAULT_FILE_SIZE			(1024*1024*1024)		//日志切换文件大小

	typedef enum WRITE_LEVEL
	{
		NORMAL_L = 0,
		WARNING_L,
		ERROR_L,
		UNKNOWN_L
	}WriteLevel;

	// Time 管理时间及时间差 以及年月日
	class LogTime {
	public:
		LogTime() :timeBegin_(time(NULL)), timeEnd_(time(NULL)) {
			time_t t = time(NULL);
			localtime_s(&tm_, &t);
		}

		void SetBeginTime() { timeBegin_ = time(NULL); }
		void SetEndTime() { timeEnd_ = time(NULL); }

		time_t GetBeginTime() { return timeBegin_; }
		time_t GetEndTime() { return timeEnd_; }
		time_t GetDeltaTime() { return timeEnd_ - timeBegin_; }
		struct tm GetCurrentDataTime() {
			time_t t = time(NULL);
			localtime_s(&tm_, &t);
			return tm_;
		};
	private:
		time_t timeBegin_;
		time_t timeEnd_;
		struct tm tm_;
	};


	class LogFile {
	public:
		LogFile();
		LogFile(const std::string& filePath);
		virtual ~LogFile();
		bool InitFunc();
		std::streampos GetFileSize() { if (!pOfsLogFile_) { return 0; }return pOfsLogFile_->tellp(); };
		void CheckFile();
		bool WriteLog(const std::string &wrtieStr, const WriteLevel wl = NORMAL_L);
		//============================
		void WriteFileTest() {
			CheckFile();
			if (!pOfsLogFile_){
				return;
			}
			(*pOfsLogFile_) << "测试1234" << std::endl;
		}
		//============================

		bool _CheckWrite();
	private:
		bool CreateFile();
		const std::string GetCurrentTimeString();
		const std::string MakeFileName();
		std::string  LogFile::GetLevelString(const WriteLevel wl);
		std::string fileName_;
		std::string filePath_;
		std::ofstream* pOfsLogFile_;
		LogTime	tm_;
	};




}//namespace DEFTOOLS 