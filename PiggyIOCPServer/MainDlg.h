// MainDlg.h : 头文件
//

#pragma once

#include "IOCPModel.h"
#include "afxdtctl.h"

enum{
	CHOOSE_PROCUDURE,
	CHOOSE_POSTHTTP,
	CHOOSE_NONE
};



// CMainDlg 对话框
class CMainDlg : public CDialog
{
// 构造
public:
	CMainDlg(CWnd* pParent = NULL);	// 标准构造函数

// 对话框数据
	enum { IDD = IDD_PIGGYIOCPSERVER_DIALOG };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV 支持


// 实现
protected:
	HICON m_hIcon;

	// 生成的消息映射函数
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	// 开始监听
	afx_msg void OnBnClickedOk();
	// 停止监听
	afx_msg void OnBnClickedStop();
	// "退出"按钮
	afx_msg void OnBnClickedCancel();
	///////////////////////////////////////////////////////////////////////
	//	系统退出的时候，为确保资源释放，停止监听，清空Socket类库
	afx_msg void OnDestroy();
	//afx_msg LRESULT OnNewMsg(WPARAM wParam,LPARAM lParam);
	DECLARE_MESSAGE_MAP()

private:

	// 初始化Socket库以及界面信息
	void Init();

	//配置
	void InitConfig();

	// 初始化List控件
	void InitListCtrl();

	void SetConfig();

public:

	// 当前客户端有新消息到来的时候，在主界面中显示新到来的信息(在类CIOCPModel中调用)
	// 为了减少界面代码对效率的影响，此处使用了内联
	inline void AddInformation(const CString strInfo)
	{
		if( ::IsWindow(GetSafeHwnd()) )
		{
			CListCtrl* pList = (CListCtrl*)GetDlgItem(IDC_LIST_INFO);
			if( pList )
			{
				pList->InsertItem(0,strInfo);
			}
		}
	}

	inline void AddDebugInfo(CString &strReadID,CString &strIP,CString &strCurrState,CString &strTime)
	{
		//return;
		CListCtrl *pList = (CListCtrl *)GetDlgItem(IDC_LIST_MSG);
		if( pList )
		{
			int iCount = pList->GetItemCount();							//行数
			int iColumn = pList->GetHeaderCtrl()->GetItemCount();		//列数
			int nIndex = -1;
			CString strItem;
			for(int row= 0;row < iCount;row++)
			{
				//for(int col= 0;col< pList->GetHeaderCtrl()->GetItemCount();col++)
				//{
				//	strItem = pList->GetItemText(row,col);
				//	if( !strItem.CompareNoCase(strStationID) || !strItem.CompareNoCase(strIP) )
				//	{
				//		nIndex = row;
				//		goto LABEL_01;
				//	}
				//}
				strItem = pList->GetItemText(row,1);
				if( !strItem.CompareNoCase(strReadID) )
				{
					nIndex = row;
					goto LABEL_01;
				}
			}
LABEL_01:
			if( nIndex != -1 )
			{
				pList->SetItemText(nIndex,1,strReadID);
				pList->SetItemText(nIndex,2,strIP);
				pList->SetItemText(nIndex,3,strCurrState);
				pList->SetItemText(nIndex,4,strTime);

				//for( int i = 0;i < iCount;i++)
				//{
				//	char szbuf[8] = {0};
				//	sprintf_s(szbuf,"%d",i);
				//	pList->SetItemText(i,0,szbuf);
				//}
			}
			else
			{	
				char szbuf[20] = {0};
				sprintf_s(szbuf,"%d",iCount);
				pList->InsertItem(iCount,szbuf);
				pList->SetItemText(iCount,1,strReadID);
				pList->SetItemText(iCount,2,strIP);
				pList->SetItemText(iCount,3,strCurrState);
				pList->SetItemText(iCount,4,strTime);
			}
		}
	}

private:

	CIOCPModel m_IOCP;                         // 主要对象，完成端口模型

public:
	afx_msg void OnBnClickedButton1();
	afx_msg void OnBnClickedRadioProcedure();

	static int m_WhichClick;
	static int GetClickVal(){return m_WhichClick;}
	afx_msg void OnBnClickedRadioPosthttp();
	afx_msg void OnBnClickedRadioNone();
	afx_msg void OnBnClickedCheck1();
	static bool m_Logflag;
	static bool GetLogFlag(){return m_Logflag;}
};
