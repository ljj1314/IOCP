// MainDlg.h : ͷ�ļ�
//

#pragma once

#include "IOCPModel.h"
#include "afxdtctl.h"

enum{
	CHOOSE_PROCUDURE,
	CHOOSE_POSTHTTP,
	CHOOSE_NONE
};



// CMainDlg �Ի���
class CMainDlg : public CDialog
{
// ����
public:
	CMainDlg(CWnd* pParent = NULL);	// ��׼���캯��

// �Ի�������
	enum { IDD = IDD_PIGGYIOCPSERVER_DIALOG };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV ֧��


// ʵ��
protected:
	HICON m_hIcon;

	// ���ɵ���Ϣӳ�亯��
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	// ��ʼ����
	afx_msg void OnBnClickedOk();
	// ֹͣ����
	afx_msg void OnBnClickedStop();
	// "�˳�"��ť
	afx_msg void OnBnClickedCancel();
	///////////////////////////////////////////////////////////////////////
	//	ϵͳ�˳���ʱ��Ϊȷ����Դ�ͷţ�ֹͣ���������Socket���
	afx_msg void OnDestroy();
	//afx_msg LRESULT OnNewMsg(WPARAM wParam,LPARAM lParam);
	DECLARE_MESSAGE_MAP()

private:

	// ��ʼ��Socket���Լ�������Ϣ
	void Init();

	//����
	void InitConfig();

	// ��ʼ��List�ؼ�
	void InitListCtrl();

	void SetConfig();

public:

	// ��ǰ�ͻ���������Ϣ������ʱ��������������ʾ�µ�������Ϣ(����CIOCPModel�е���)
	// Ϊ�˼��ٽ�������Ч�ʵ�Ӱ�죬�˴�ʹ��������
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
			int iCount = pList->GetItemCount();							//����
			int iColumn = pList->GetHeaderCtrl()->GetItemCount();		//����
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

	CIOCPModel m_IOCP;                         // ��Ҫ������ɶ˿�ģ��

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
