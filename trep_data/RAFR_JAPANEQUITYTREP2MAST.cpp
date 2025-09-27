/*
*	@file				RAFR_JAPANEQUITYTREP2MAST.h
*	@author			D.Y.Choi
*	@date				2016.01.05
*	@brief			RAFR_JAPANEQUITYTREP2MAST.h
*	@remark			-------------------------------------------------------------------------
*	@remark				No				Update				Author					Contents
*	@remark			-------------------------------------------------------------------------
*	@remark				1					2016.01.05		D.Y.Choi				New
*	@remark			-------------------------------------------------------------------------
*/
#ifndef RAFR_JAPANEQUITYTREP2MAST_CPP_
#define RAFR_JAPANEQUITYTREP2MAST_CPP_

#include "RAFR_JAPANEQUITYTREP2MAST.h"
#include "common/Common.cpp"
#include "common/ProcID.cpp"

#include "procattr/procattr.h"
#include "procstat/procstat.h"

#include "fid2sidmap/fidbuf.h"
#include "fid2sidmap/f2sconv.h"
#include "sid2sidmap/s2sconv.h"

#include "filelist/filelistBody.h"
#include "filelist/filelist.h"

#include "eventNotify/eventNotify.h"

#include "StructDataHelper.h"

#include <sys/time.h>
#include <unistd.h>
#include <math.h>

extern int convF2S(fidbuf *FIDBUF, int fid, StructDataHelper *helper, char *name);
extern int convF2M(fidbuf *FIDBUF, int fid, StructDataHelper *helper, bool force);
extern int convAllF2M(fidbuf *FIDBUF, StructDataHelper *helper, bool force);

#define F2M(fid,name) convF2S(FIDBUF, fid, &master_, (char*)name)

ACE_BEGIN_VERSIONED_NAMESPACE_DECL

RAFR_JAPANEQUITYTREP2MAST::RAFR_JAPANEQUITYTREP2MAST ()
	:	outfile_recsize_(0),
		writelock_(false),
		current_timact_(-1),
		before_timact_(-1),
		trdprc_fid_(6),
		trdvol_fid_(178),
		open_prc_fid_(19),
		high_prc_fid_(12),
		low_prc_fid_(13),
		close_prc_fid_(3372)
{
}

RAFR_JAPANEQUITYTREP2MAST::~RAFR_JAPANEQUITYTREP2MAST ()
{
}

int RAFR_JAPANEQUITYTREP2MAST::open (void)
{
	// ACE_TRACE("open");

	// ACE_DEBUG((LM_DEBUG, "[%D][%P][%t][%N.%l] open()\n"));

	// master cfg
	{
		// 2019.10.16
		if (PROCATTR->master_count() > 0)
		{
			this->jmk_cfg_ = PROCATTR->master_cfg((u_int)0);
		}
		else
		{
			string key = "PROCESS.JMK_CFG";
			if ( CONFIG_EXIST((char*)key.c_str()) )
			{
				ACE_ARGV args((const char*)CONFIG_GET_S((char*)key.c_str()));
				this->jmk_cfg_ = args.argv()[0];
				ACE_DEBUG((LM_INFO,
						"[%D][%P][%t][%N.%l] CFG [%s] IS [%s].\n",
						key.c_str(),
						this->jmk_cfg_.c_str()));
			}
			else
			{
				ACE_DEBUG((LM_ERROR,
						"[%D][%P][%t][%N.%l] CFG [%s] NOT DEFINED.\n",
						key.c_str()));
				EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_ERROR,
									EVENTNOTIFY_ALARM_NO,
									"CONFIG %s NOT DEFINED IN JSON.",
									key.c_str());
				return -1;
			}
		}
	}

	// FID MARKET DATA SUPPLIER
	// ETC CONFIG
	{
		string key = "PROCESS.FID_MDS";
		if ( CONFIG_EXIST((char*)key.c_str()) )
		{
			this->fid_mds_ = (const char*)CONFIG_GET_S((char*)key.c_str());
			ACE_DEBUG((LM_INFO,
					"[%D][%P][%t][%N.%l] CFG [%s] IS [%s].\n",
					key.c_str(),
					this->fid_mds_.c_str()));
		}
		else
		{
			ACE_DEBUG((LM_ERROR,
					"[%D][%P][%t][%N.%l] CFG [%s] NOT DEFINED.\n",
					key.c_str()));
			EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_ERROR,
								EVENTNOTIFY_ALARM_NO,
								"CONFIG %s NOT DEFINED IN JSON.",
								key.c_str());
			return -1;
		}
	}

	/* *********************************************************************************** */
	{
		string key;
		if (PROCATTR->mapping_count() > 0)
		{
			for(u_int i=0;i < PROCATTR->mapping_count();i++)
			{
				ACE_DEBUG((LM_DEBUG, "[%D][%P][%t][%N.%l] MAPPING_PURPOSE[%d] [%s].\n"
						,i , PROCATTR->mapping_purpose(i)));

				if(strncmp(PROCATTR->mapping_purpose(i), "M2DQ", 4) == 0)
				{
					this->out_sise_mid_ = PROCATTR->mapping_s2smap_mid(i);
					this->out_sise_ssi_ = PROCATTR->mapping_s2smap_ssi(i);
				}
				else if(strncmp(PROCATTR->mapping_purpose(i), "M2DM", 4) == 0) // MASTER TO DIST MASTER
				{
					this->out_master_mid_ = PROCATTR->mapping_s2smap_mid(i);
					this->out_master_ssi_ = PROCATTR->mapping_s2smap_ssi(i);
				}
				else if(strncmp(PROCATTR->mapping_purpose(i), "T2MM", 4) == 0) // FID2SID MAP KEY
				{
					this->fmi_ = PROCATTR->mapping_f2smap_fmi(i);
					this->mds_ = PROCATTR->mapping_f2smap_mds(i);
				}
			}
		}
		else
		{
			ACE_DEBUG((LM_ERROR,
					"[%D][%P][%t][%N.%l] CFG [%s] NOT DEFINED.\n",
					key.c_str()));
			EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_ERROR,
								EVENTNOTIFY_ALARM_NO,
								"CONFIG %s NOT DEFINED IN JSON.",
								key.c_str());
			return -1;
		}
	}
	/* *********************************************************************************** */
	/* *********************************************************************************** */
	// 2019.11.20
	// ETC CONFIG
	{
		string key = "PROCESS.WRITELOCK";
		if ( CONFIG_EXIST((char*)key.c_str()) )
		{
			this->writelock_ = (bool)CONFIG_GET_I((char*)key.c_str());
			ACE_DEBUG((LM_INFO,
					"[%D][%P][%t][%N.%l] CFG [%s] IS [%s].\n",
					key.c_str(),
					(this->writelock_) ? "YES" : "NO"));
		}
	}
	/* *********************************************************************************** */
	//
	int rc = 0;
	rc = this->SIDSPEC().initialize();
	if (rc)
	{
		ACE_DEBUG((LM_ERROR,
				"[%D][%P][%t][%N.%l] SIDSPEC Initialize ERROR. [%s]\n"));
		EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_ERROR,
							EVENTNOTIFY_ALARM_NO,
							"SIDSPEC MASTER INITIALIZE ERROR.");
		return -1;
	}

	ACE_DEBUG((LM_INFO,
			"[%D][%P][%t][%N.%l] SIDSPEC Initialize OK. [%s]\n"));
	/* *********************************************************************************** */
	//
	rc = this->JMK().initialize(this->jmk_cfg_.c_str());
	if (rc)
	{
		ACE_DEBUG((LM_ERROR,
				"[%D][%P][%t][%N.%l] JMK Initialize ERROR. [%s]\n",
				this->jmk_cfg_.c_str()));
		EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_ERROR,
							EVENTNOTIFY_ALARM_NO,
							"JMK MASTER INITIALIZE ERROR. [%s]",
							this->jmk_cfg_.c_str());
		return -1;
	}

	ACE_DEBUG((LM_INFO,
			"[%D][%P][%t][%N.%l] JMK Initialize OK. [%s]\n",
			this->jmk_cfg_.c_str()));
	/* *********************************************************************************** */
	// outfile OPEN
	{
		// 2019.10.16
		/* *************************************************************************** */
		string outfile;
		if (PROCATTR->outfile_count() > 0)
		{
			this->outfile_ = PROCATTR->outfile_path((u_int)0);
			this->outfile_recsize_ = PROCATTR->outfile_logical_record_length((u_int)0);
		}
		else
		{
			ACE_CString filelist_key;
			ACE_CString cfg_outfile = "PROCESS.OUT_FILE";
			if ( CONFIG_EXIST((char*)cfg_outfile.c_str()) )
			{
				filelist_key = (const char*)CONFIG_GET_S((char*)cfg_outfile.c_str());
				ACE_DEBUG((LM_INFO,
						"[%D][%P][%t][%N.%l] CFG [%s] IS [%s].\n",
						cfg_outfile.c_str(),
						filelist_key.c_str()));

				filelist FILELIST;
				rc = FILELIST.initialize();
				if (rc)
				{
					ACE_DEBUG((LM_ERROR,
							"[%D][%P][%t][%N.%l] FILELIST MASTER INITIALIZE ERROR.\n"));
					EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_ERROR,
										EVENTNOTIFY_ALARM_NO,
										"FILELIST MASTER INITIALIZE ERROR.");
					return -1;
				}

				filelistBody filelistbody;
				rc = FILELIST.info_filelist(filelist_key.c_str(), filelistbody);
				if (rc)
				{
					ACE_DEBUG((LM_ERROR,
							"[%D][%P][%t][%N.%l] FILELIST GET ERROR. KEY:%s\n",
							filelist_key.c_str()));
					EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_ERROR,
										EVENTNOTIFY_ALARM_NO,
										"FILELIST MASTER GET ERROR. NOT FOUND. [%s]",
										filelist_key.c_str());
					return -1;
				}

				char hostname[64+1];

				rc = gethostname(hostname, sizeof(hostname));
				if (rc)
				{
					ACE_DEBUG((LM_ERROR,
							"[%D][%P][%t][%N.%l] GET HOSTNAME ERROR. (%d)%s\n",
							errno,
							strerror(errno)));
					EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_ERROR,
										EVENTNOTIFY_ALARM_NO,
										"HOSTNAME GET ERROR. [%d][%s]",
										errno,
										strerror(errno));
					return -1;
				}

				string outfile_path;
				if (strlen(filelistbody.system()) > 0)
				{
					// 2020.01.21 d.y.choi MAX - 1 로 변경
					// remote
					if (memcmp(filelistbody.system(), hostname, MAX(strlen(filelistbody.system()), strlen(hostname)) - 1) != 0)
					{
						outfile_path = string("\\") + string(filelistbody.system()) + string(".") + string(filelistbody.path());
					}
					else
						outfile_path = filelistbody.path();
				}
				else
				{
					outfile_path = filelistbody.path();
				}

				ACE_ARGV args(outfile_path.c_str());

				this->outfile_ = args.argv()[0];
				this->outfile_recsize_ = filelistbody.logical_record_length();
			}
		}

		if (this->outfile_.length() > 0)
		{
			rc = this->NFSWRITE().open(this->outfile_);
			if (rc)
			{
				ACE_DEBUG((LM_ERROR,
						"[%D][%P][%t][%N.%l] OPEN OUT-FILE ERROR. %s\n",
						this->outfile_.c_str()));
				EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_ERROR,
									EVENTNOTIFY_ALARM_YES,
									"OUTFILE [%s] OPEN ERROR.",
									this->outfile_.c_str());
				return 0;
			}

			ACE_DEBUG((LM_INFO,
					"[%D][%P][%t][%N.%l] OPEN OUTFILE OK. LOGICAL-RECORD-SIZE: [%Q] PATH: [%s]\n",
					this->outfile_recsize_,
					this->outfile_.c_str()));

/** 
			EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_INFO,
								EVENTNOTIFY_ALARM_NO,
								"OUTFILE OPEN OK. [PATH:%s] [RECSIZE:%lu]",
								this->outfile_.c_str(),
								this->outfile_recsize_);
**/
		}
	}

	// 2021.02.25 ADD
	master_.setItems( gItems_MMP_EQUITY_MASTER, gItemCount_MMP_EQUITY_MASTER);

	out_master_.setItems( gItems_EQUITY_MASTER, gItemCount_EQUITY_MASTER);
	out_sise_.setItems( gItems_EQUITY_SISE, gItemCount_EQUITY_SISE);

	// Out message buffer set
	memset(sambuf_, 0x20, sizeof(sambuf_));
	out_sise_.setData(sambuf_);
	omsg_ = (EQUITY_SISE*)sambuf_;

	// Set fixed items
	sambuf_[sizeof(sambuf_)-1] = '\n';

	memcpy(omsg_->DATA_GB, "A3", 2);
	memcpy(omsg_->INFO_GB, "22", 2);
	omsg_->MKT_GB = 'B';

	omsg_->SESSION_GB = '0';

	omsg_->TRD_GB = '0';
	memset(omsg_->AFTMKT_PRC, 0x20, sizeof(omsg_->AFTMKT_PRC));
	memset(omsg_->FILLER, 0x20, sizeof(omsg_->FILLER));

	omsg_->FF = 0xff;

	/**
	typedef struct {
		char eng_nm[44];	// English name
		short fid;			// FID
		char rtype;			// reset type
		char dpos;			// decimal position
	} FID_INFO ;
	**/

	FID_INFO finfo[] = { 
		{ "TRD_PRC", 6, RESET_ZERO, 1},
		{ "NET_CHNG", 11, RESET_ZERO, 1},
		{ "HIGH_PRC", 12, RESET_ZERO, 1},
		{ "LOW_PRC", 13, RESET_ZERO, 1},
		{ "TRD_DT", 16, RESET_SPACE, -1},
		{ "LOCAL_TM", 18, RESET_SPACE, -1},
		{ "OPEN_PRC", 19, RESET_ZERO, 1},
		{ "HST_CLOSE_PRC", 21, RESET_ZERO, 1},		
		{ "BID_PRC", 22, RESET_ZERO, 1},
		{ "ASK_PRC", 25, RESET_ZERO, 1},
		{ "BID_SIZE", 30, RESET_ZERO, 0},
		{ "ASK_SIZE", 31, RESET_ZERO, 0},
		{ "SVOL", 32, RESET_ZERO, 0},
		{ "EARNINGS", 34, RESET_ZERO, 1},
		{ "YIELD", 35, RESET_ZERO, 2},
		{ "PERATIO", 36, RESET_ZERO, 2},
		{ "DIVPAYDATE", 38, RESET_SPACE, -1},
		{ "EX_DIVIDEND_DT", 39, RESET_SPACE, -1},
		{ "ORDER_SZ", 55, RESET_ZERO, 0},
		{ "PCT_CHNG", 56, RESET_ZERO, 2},
		{ "BID_PRC_CLOSE", 60, RESET_ZERO, 1},
		{ "ASK_PRC_CLOSE", 61, RESET_ZERO, 1},
		{ "DIVDEND", 71, RESET_ZERO, 3},
		{ "UPLIMIT", 75, RESET_ZERO, 1},
		{ "DNLIMIT", 76, RESET_ZERO, 1},
		{ "HST_CLOSE_DT", 79, RESET_SPACE, -1},
		{ "TRD_VOL", 178, RESET_ZERO, 0},
		{ "ORDER_SZ_A", 198, RESET_ZERO, 0},		
		{ "SETTLEDATE", 288, RESET_SPACE, -1},
		{ "SAL_TM", 379, RESET_SPACE, -1},
		{ "SAMT_SC", 380, RESET_ZERO, 0},	
		{ "ACT_FLAG1", 975, RESET_SPACE, -1},
		{ "BASE_PRC", 1465, RESET_ZERO, 1},
		{ "SEC_GB", 2156, RESET_SPACE, -1},
		{ "52WK_HIGH", 3265, RESET_ZERO, 1},
		{ "52WK_LOW", 3266, RESET_ZERO, 1},
		{ "CLOSE_PRC", 3372, RESET_ZERO, 1},
		{ "52W_HDAT", 3448, RESET_SPACE, -1},
		{ "52W_LDAT", 3450, RESET_SPACE, -1},
		{ "ISIN_CODE", 3655, RESET_SPACE, -1},
		{ "MKT_SEGMNT", 3842, RESET_SPACE, -1},
		{ "TRD_STATUS", 6614, RESET_SPACE, -1},
		{ "OFF_CLS_DT", 6762, RESET_SPACE, -1},
		//{ "LMT_STATUS", 13494, RESET_SPACE, -1},
		{ "SAMT", 32741, RESET_ZERO, 0},		
	};

	// Set FID info
	for(int i= 0; finfo[i].fid > 0; i++)
	{
		master_.setpItems(&finfo[i]);
	}	

	// Initialize trd_unit to 999
	trd_unit_ = 999;


	this->msg_queue()->high_water_mark(1 * 1024 * 1024);
	this->msg_queue()->low_water_mark (1 * 1024 * 1024);

	PROCSTAT_INSTANCE->set_conn_state(PROCSTAT_CONN_CONN);
	PROCSTAT_INSTANCE->set_if_start(PROCATTR_YN_YES);
	PROCSTAT_INSTANCE->set_state(PROCSTAT_STATE_RUNNING);

	return 0;
}

int RAFR_JAPANEQUITYTREP2MAST::get_prcd_bytes(off_t* prcd_bytes)
{
	*prcd_bytes = PROCSTAT->out_bytes;
	return 0;
}

int RAFR_JAPANEQUITYTREP2MAST::handle_msg (ACE_Time_Value *timeout)
{
	// ACE_TRACE("write_file");

	// ACE_DEBUG((LM_DEBUG, "[%D][%P][%t][%N.%l] write_file(%d)\n", timeout->sec()));

	ssize_t transd_bytes = 0;
	ACE_Message_Block *mblk = 0;

	time_t tt;
	time(&tt);
	tt += timeout->sec();
	ACE_Time_Value tm(tt);
	if (this->getq (mblk, &tm) < 0) return 0;
	// if (this->getq (mblk) < 0) return 0;

	// Stop Message ���� üũ
	if (mblk->size () == 0 &&
		mblk->msg_type () == ACE_Message_Block::MB_STOP)
	{
		mblk->release();
		ACE_DEBUG((LM_WARNING,
				"[%D][%P][%t][%N.%l] STOP MESSAGE RECEIVED.\n"));
		return -2;
	}

	transd_bytes = process_msg(mblk->rd_ptr(), mblk->length());

	mblk->release();

	return transd_bytes;

}

void RAFR_JAPANEQUITYTREP2MAST::set_date(int ymd)
{
	user_stime_.tm_year = ymd / 10000 - 1900;
	user_stime_.tm_mon = (ymd % 10000) / 100 - 1;
	user_stime_.tm_mday = ymd % 100;
	user_stime_.tm_isdst = 0;
}

char* RAFR_JAPANEQUITYTREP2MAST::set_time(int hms, int gmt_second)
{
	time_t user_time;
	struct tm t_tm;
	char time_temp[6+1];

	user_stime_.tm_hour = hms / 10000 ;
	user_stime_.tm_min = (hms % 10000) / 100;
	user_stime_.tm_sec = hms % 100 ;

	user_time = mktime(&user_stime_);
	user_time += gmt_second;

	localtime_r(&user_time, &t_tm);

	sprintf(time_temp, "%02d%02d%02d", t_tm.tm_hour, t_tm.tm_min, t_tm.tm_sec);

	return strdup(time_temp);
}

char* RAFR_JAPANEQUITYTREP2MAST::getDateTime()
{
	time_t		t_t;
	struct tm	t_tm;

	time(&t_t);
	localtime_r(&t_t, &t_tm);

	char* datetime = (char *)malloc(14+1);

	sprintf(datetime,
		"%04d%02d%02d%02d%02d%02d",
		t_tm.tm_year+1900,
		t_tm.tm_mon+1,
		t_tm.tm_mday,
		t_tm.tm_hour,
		t_tm.tm_min,
		t_tm.tm_sec);

	//return strdup(datetime);
	return datetime;
}

int RAFR_JAPANEQUITYTREP2MAST::process_msg (const char* buf, size_t length)
{
	// ACE_TRACE("process_msg");

	// ACE_DEBUG((LM_DEBUG, "[%D][%P][%t][%N.%l] process_msg(%d)%.1024s\n", length, buf));

	int rc = 0;
	const char* fid_key = 0;

	size_t mlength = 0;
	ssize_t transd_bytes = 0;

	char* jmk_buf = 0;

	// refresh/update(99999)
	unsigned long datatype_fid = FIDBUF_DATATYPE_FID;
	FID_ENTRY* datatype_entry = 0;
	bool isUpdateData = true;

	// ric(0)
	unsigned long ric_fid = FIDBUF_KEY_FID;
	FID_ENTRY* entry = 0;

	// 2023.02.03 ADD
	unsigned long sise_hoga_msg_fid = 99997;
	FID_ENTRY* sise_hoga_msg_entry = 0;

	// DIVDEND : FID(71), 배당금
	unsigned long divdend_fid = 71;
	FID_ENTRY* divdend_entry = 0;

	// DIVDEND : FID(38), 배당일
	unsigned long divpaydate_fid = 38;
	FID_ENTRY* divpaydate_entry = 0;

	// DIVDEND : FID(35), 배당율
	unsigned long yield_fid = 35;
	FID_ENTRY* yield_entry = 0;

	bool update_flag = true;

	fidbuf* FIDBUF = 0;
	FIDBUF = new fidbuf;
	if (FIDBUF == 0)
	{
		ACE_DEBUG((LM_ERROR,
				"[%D][%P][%t][%N.%l] fidbuf malloc ERROR. [%d]%s\n",
				errno,
				strerror(errno)));
		rc = -1;
		update_flag = false;
		goto last;
	}

	rc = FIDBUF->parse(buf, length);
	if (rc)
	{
		ACE_DEBUG((LM_ERROR,
				"[%D][%P][%t][%N.%l] PARSE FID ERROR. [%.1024s]\n",
				buf));
		EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_ERROR,
							EVENTNOTIFY_ALARM_NO,
							"PARSE FID ERROR.");
		rc = 1;
		update_flag = false;
		goto last;
	}

	// 2022.08.16 DELETE
	//FIDBUF->dump();

	// REFRESH/UPDATE ����
	datatype_entry = FIDBUF->find(datatype_fid);
	if (datatype_entry == 0)
	{
		ACE_DEBUG((LM_ERROR,
				"[%D][%P][%t][%N.%l] REFRESH/UPDATE FID NOT FOUND. [FID:%Q]\n",
				datatype_fid));
		EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_ERROR,
							EVENTNOTIFY_ALARM_NO,
							"REFRESH/UPDATE RIC NOT FOUND. [FID:%Q]",
							datatype_fid);
		rc = 1;
		update_flag = false;
		goto last;
	}
	else
	{
		if (strncasecmp(datatype_entry->value, FIDBUG_DATATYPE_REFRESH, strlen(FIDBUG_DATATYPE_REFRESH)) == 0)
			isUpdateData = false;
	}

	// find RIC (KEY)
	entry = FIDBUF->find(ric_fid);
	if (entry == 0)
	{
		ACE_DEBUG((LM_ERROR,
				"[%D][%P][%t][%N.%l] RICCODE FID NOT FOUND. [FID:%Q]\n",
				ric_fid));
		EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_ERROR,
							EVENTNOTIFY_ALARM_NO,
							"RICCODE RIC NOT FOUND. [FID:%Q]",
							ric_fid);
		rc = 1;
		update_flag = false;
		goto last;
	}

	// 2023.02.03 ADD
	sise_hoga_msg_entry = FIDBUF->find(sise_hoga_msg_fid);

	// change delay RIC Code
	rc = FIDBUF->change_delay_riccode(ric_fid);
	if (rc)
	{
		ACE_DEBUG((LM_ERROR,
				"[%D][%P][%t][%N.%l] CHANGE DELAY RIC CODE ERROR. FID:%Q, RIC:[%Q]%s\n",
				entry->fid,
				entry->value_length,
				entry->value));
		EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_ERROR,
							EVENTNOTIFY_ALARM_NO,
							"CHANGE DELAY RIC CODE ERROR. [FID:%lu] RIC:[%lu]%s",
							entry->fid,
							entry->value_length,
							entry->value);
		//2020.03.16
		rc = 1;
		update_flag = false;
		goto last;
	}

	fid_key = entry->value;

	// read master
	rc = this->JMK().info_jmk(fid_key, &mlength, (void**)&jmk_buf);
	if (rc)
	{
		ACE_DEBUG((LM_ERROR,
				"[%D][%P][%t][%N.%l] JMK READ ERROR. NOT FOUND. [%s][%d]. SKIP\n",
				fid_key, rc));
		//2020.03.16
		rc = 1;
		update_flag = false;
		goto last;
	}
	//else
	//{
	//	ACE_DEBUG((LM_DEBUG,
	//			"[%D][%P][%t][%N.%l] JMK READ OK. [%s].[%Q][%.1024s]\n",
	//			fid_key,
	//			mlength,
	//			jmk_buf));
	//}

	master_.setData(jmk_buf);
	pmaster_ = (MMP_EQUITY_MASTER*)master_.getPdata();

	// Fill outbuf at first data
	if(trd_unit_ == 999) {
		trd_unit_ = master_.getv("TRD_UNITS").ival();
		gmt_second_ = master_.getv("GMT_SECOND").ival();
		memcpy(omsg_->EXCHG_CD, pmaster_->EXCHG_CD, sizeof(omsg_->EXCHG_CD));
		memcpy(omsg_->LOCAL_DT, master_.getv("BUSINESS_DAY").cstr(), sizeof(omsg_->LOCAL_DT));
		memcpy(omsg_->KOR_DT, omsg_->LOCAL_DT, sizeof(omsg_->KOR_DT));
		businessdate_ = master_.getv("BUSINESS_DAY").ival();
		set_date(businessdate_);
	}

	// Fill some items of master_ at first data
	if(!master_.getv(18).compare("      ")) {
		// Do something
		//master_.resetv(3520, false);
	}

	// Check value change
	// false : check value change before update
	// true : update without checking value change
	convAllF2M(FIDBUF, &master_, false);


	//2020.06.05 ADD
	//시가/고가/저가 시간 Setting
	if (isUpdateData)
	{
		// 2020.06.05
		if(master_.isChanged(open_prc_fid_) && master_.getv(open_prc_fid_).ival() > 0)
		{
			if (master_.getv("SAL_TM").compare("      ") != 0)
				memcpy(pmaster_->OPEN_PRC_TM, pmaster_->SAL_TM, sizeof(pmaster_->OPEN_PRC_TM));
			else
				memcpy(pmaster_->OPEN_PRC_TM, pmaster_->LOCAL_TM, sizeof(pmaster_->OPEN_PRC_TM));
		}

		if(master_.isChanged(high_prc_fid_) && master_.getv(high_prc_fid_).ival() > 0)
		{
			if (master_.getv("SAL_TM").compare("      ") != 0)
				memcpy(pmaster_->HIGH_PRC_TM, pmaster_->SAL_TM, sizeof(pmaster_->OPEN_PRC_TM));
			else
				memcpy(pmaster_->HIGH_PRC_TM, pmaster_->LOCAL_TM, sizeof(pmaster_->OPEN_PRC_TM));
		}

		if(master_.isChanged(low_prc_fid_) && master_.getv(low_prc_fid_).ival() > 0)
		{
			if (master_.getv("SAL_TM").compare("      ") != 0)
				memcpy(pmaster_->LOW_PRC_TM, pmaster_->SAL_TM, sizeof(pmaster_->OPEN_PRC_TM));
			else
				memcpy(pmaster_->LOW_PRC_TM, pmaster_->LOCAL_TM, sizeof(pmaster_->OPEN_PRC_TM));
		}

	}

	if (master_.get("SEND_MASTER_MAX") >= 1
	// 2024.03.13 DELETE
	// && master_.getv("UPLIMIT").compare("            ") != 0
	// && master_.getv("DNLIMIT").compare("            ") != 0
	 && isUpdateData == false)
	{
		if (master_.get("SEND_MASTER_COUNT") == 0
		 // 2023.02.03 ADD
		 && sise_hoga_msg_entry
		 && !IS_RESET(sise_hoga_msg_entry->value)
		 && memcmp(sise_hoga_msg_entry->value, "S", sise_hoga_msg_entry->value_length) == 0)
		{
			// DIVDEND : FID(71), 배당금
			divdend_entry = FIDBUF->find(divdend_fid);

			// DIVDEND : FID(38), 배당일
			divpaydate_entry = FIDBUF->find(divpaydate_fid);

			// DIVDEND : FID(35), 배당율
			yield_entry = FIDBUF->find(yield_fid);

			dist_divdend_flag_ = false;

			// 값이 모두 있을때만 분배함.
			// 2024.09.23 ADD - divdend_entry && divpaydate_entry yield_entry
		 	if ((divdend_entry && !IS_RESET(divdend_entry->value))
		 	 && (divpaydate_entry && !IS_RESET(divpaydate_entry->value))
		 	 && (yield_entry && !IS_RESET(yield_entry->value)))
				dist_divdend_flag_ = true;

			master_.setv("DIST_FLAG", (char *)"Y");
			rc = process_master_outfile();
			if (rc)
			{
				ACE_DEBUG((LM_ERROR,
						"[%D][%P][%t][%N.%l] PROCESS MASTER ERROR. [%s].\n",
						fid_key));
			}
		}
	}

	// 2022.08.08 ADD : isUpdateData == true
	if (isUpdateData == true)
	{
		// Regard as trade if trade_price or svol is changed
		if(master_.isChanged(trdprc_fid_) || master_.isChanged(32))
		{
			rc = process_sise_outfile();
			if (rc)
			{
				ACE_DEBUG((LM_ERROR,
						"[%D][%P][%t][%N.%l] OUTFILE WRITE ERROR. [%s].\n",
						fid_key));
				// 2022.08.08 DELETE
				//goto last;
			}
		}
		else if(FIDBUF->find(close_prc_fid_) != 0)	// Ending without trade
		{
			if(FIDBUF->find(379) == 0) {			// set time to 150000 KST
				master_.setv(379, (char *)"060000", true);				
			}
			if (!master_.isChanged(32))	// if svol is not changed, set trdvol to 0
			{
				master_.setv(trdvol_fid_, 0, 0);
			}
			// set trd_prc with close_prc
			if(memcmp(pmaster_->TRD_PRC, pmaster_->CLOSE_PRC, sizeof(pmaster_->TRD_PRC)))
				memcpy(pmaster_->TRD_PRC, pmaster_->CLOSE_PRC, sizeof(pmaster_->TRD_PRC));
			master_.setv(11, 0, trd_unit_);			// set net_chg to 0

			rc = process_sise_outfile();
			if (rc)
			{
				ACE_DEBUG((LM_ERROR,
						"[%D][%P][%t][%N.%l] OUTFILE WRITE ERROR. [%s].\n",
						fid_key));
				// 2022.08.08 DELETE
				//goto last;
			}

		}
		else
		{
			rc = 1;
			update_flag = false;
		}
	}
	else
	{
		rc = 1;
	}

last:
	if (update_flag)
	{
		master_.set("UPDATE_COUNT", master_.get("UPDATE_COUNT") + 1);

		int rtn = this->JMK().altersafe_jmk_changed(fid_key, mlength, jmk_buf, &master_);
		if (rtn<0)
		{
			ACE_DEBUG((LM_ERROR,
					"[%D][%P][%t][%N.%l] JMK UPDATE ERROR. [%s]. SKIP\n",
					fid_key));

			EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_ERROR,
								EVENTNOTIFY_ALARM_NO,
								"JMK MASTER UPDATE ERROR. [RIC:%s]",
								fid_key);
		}
		//else
		//{
		//	ACE_DEBUG((LM_DEBUG,
		//			"[%D][%P][%t][%N.%l] JMK UPDATE OK. [%s].[%Q][%.1024s]\n",
		//			fid_key,
		//			mlength,
		//			jmk_buf));
		//}
	}

	// 2020.02.26 d.y.choi
	if (FIDBUF)
	{
		FIDBUF->release();
		delete FIDBUF;
	}

	transd_bytes = length;

	PROCSTAT->out_count++;
	PROCSTAT->out_bytes += transd_bytes;
	PROCSTAT->ifseq++;

	if (rc == 0)
	{
		PROCSTAT->proccnt++;
	}
	else
	if (rc > 0)
	{
		PROCSTAT->skipcnt++;
	}
	else
	{
		PROCSTAT->failcnt++;
	}

	return transd_bytes;

}

int RAFR_JAPANEQUITYTREP2MAST::process_master_outfile ()
{
	// ACE_DEBUG((LM_DEBUG, "[%D][%P][%t][%N.%l] process_master_outfile()\n"));

	if (this->outfile_.length() == 0) return 0;

	char* sambuf = (char*)malloc(this->outfile_recsize_);
	memset(sambuf, 0x20, this->outfile_recsize_);
	sambuf[this->outfile_recsize_-1] = '\n';

	EQUITY_MASTER *omaster = (EQUITY_MASTER*)sambuf;

	out_master_.setData(sambuf);

	int trd_unit = master_.getv("TRD_UNITS").ival();

	out_master_.setv("DATA_GB", (char *)"A0");
	out_master_.setv("INFO_GB", (char *)"22");
	out_master_.setv("MKT_GB",  (char *)"B");
	out_master_.setv("EXCHG_CD",  master_.getv("EXCHG_CD"));

	char *dateTime = getDateTime();
	out_master_.setv("TRANS_TM", dateTime);
	free(dateTime);

	out_master_.setv("CUR_CD", 	master_.getv("CUR_CD"));
	out_master_.leftjustify((char*)"RIC_CD",	master_.getv("RIC_CD"));
	out_master_.leftjustify((char*)"SYMBOL_CD",	master_.getv("SYMBOL_CD"));
	if (master_.getv("ISIN_CD").compare("            ") == 0)
		out_master_.leftjustify((char*)"ISIN_CD",	master_.getv("ISIN_CODE"));
	else
		out_master_.leftjustify((char*)"ISIN_CD",	master_.getv("ISIN_CD"));
		
	out_master_.leftjustify((char*)"KOR_NM",	master_.getv("KOR_NM"));
	out_master_.leftjustify((char*)"ENG_NM",	master_.getv("ENG_NM"));

	double shares_vol = master_.getv("SHARES_VOL").dval();
	out_master_.setv("SHARES_VOL",	shares_vol, 0);
	
	out_master_.setv("TRD_DT",	master_.getv("BUSINESS_DAY"));
	out_master_.setv("SHARES_DT",	master_.getv("SHARES_DT"));
	out_master_.setv("HST_CLOSE_DT",master_.getv("HST_CLOSE_DT"));
	out_master_.setv("BASE_PRC",	master_.getv("BASE_PRC"), trd_unit);
	out_master_.setv("HST_CLOSE_PRC",master_.getv("HST_CLOSE_PRC"), trd_unit);
	out_master_.setv("UPLIMIT",	master_.getv("UPLIMIT"), trd_unit);
	out_master_.setv("DNLIMIT",	master_.getv("DNLIMIT"), trd_unit);
	// 2021.12.02 EDIT
	//out_master_.setv("FACE_PRC",	master_.getv("FACE_PRC"), trd_unit);
	out_master_.left_space2zero((char*)"FACE_PRC",	master_.getv("FACE_PRC"));
	//out_master_.leftjustify((char*)"FACE_PRC",	master_.getv("FACE_PRC"));

	out_master_.setv("TRD_UNITS",	master_.getv("TRD_UNITS"));
	out_master_.setv("SEC_GB",	master_.getv("SEC_GB"));
	out_master_.setv("EQY_GB",	master_.getv("EQY_GB"));
	out_master_.leftjustify((char*)"IND_GR_GB",	master_.getv("IND_GR_GB"));

	if (master_.getv("TRD_STATUS").ival() == 2)
		out_master_.setv("TRD_STATUS", (char *)"2");
	else if (master_.getv("TRD_STATUS").ival() == 3)
		out_master_.setv("TRD_STATUS", (char *)"3");
	else
		out_master_.setv("TRD_STATUS", (char *)" ");
	
	out_master_.setv("ISSUE_FLAG",	master_.getv("ISSUE_FLAG"));
	out_master_.leftjustify((char*)"EQY_DETAIL_GB",	master_.getv("EQY_DETAIL_GB"));

	// 2021.05.17 DOUBLE 계산상 오류 처리
	//double cap_samt = (master_.getv("SHARES_VOL").dval() * master_.getv("BASE_PRC").dval());
	double cap_samt = (master_.getv("SHARES_VOL").dval() * master_.getv("BASE_PRC").dval()) + 0.00001;
	out_master_.setv("CAP_SAMT", trunc(cap_samt), 0);

	out_master_.setv("52WK_HIGH",	master_.getv("52WK_HIGH"), trd_unit);
	out_master_.setv("52WK_LOW",	master_.getv("52WK_LOW"), trd_unit);
	out_master_.setv("52W_HDAT",	master_.getv("52W_HDAT"));
	out_master_.setv("52W_LDAT",	master_.getv("52W_LDAT"));

	if (dist_divdend_flag_ == false)
	{
		out_master_.setv("DIVDEND", (double)0, 4);
		out_master_.setv("DIVPAYDATE", (char *)" ");
		out_master_.setv("YIELD", (double)0, 4);
	}
	else
	{
		out_master_.setv("DIVDEND", master_.getv("DIVDEND"), 4);

		if (memcmp(master_.getv("DIVPAYDATE").cstr()+6, (char*)"00", 2) == 0)
		{
			out_master_.setv("DIVPAYDATE",	master_.getv("DIVPAYDATE"));
			omaster->DIVPAYDATE[master_.getv("DIVPAYDATE")._len-1] = '1';
		}
		else
		{
			out_master_.setv("DIVPAYDATE",	master_.getv("DIVPAYDATE"));
		}

		out_master_.setv("YIELD", master_.getv("YIELD"), 4);
	}

	out_master_.setv("EARNINGS",	master_.getv("EARNINGS"), 4);
	out_master_.setv("PERATIO",	master_.getv("PERATIO"), 4);
	out_master_.setv("CAP_SAMT_CUR_CD",	master_.getv("CAP_SAMT_CUR_CD"));
	out_master_.setv("NATION_CD",	master_.getv("NATION_CD"));
	out_master_.setv("SEDOL_CD",	master_.getv("SEDOL_CD"));
	out_master_.setv("MARKET_CAP",	master_.getv("MARKET_CAP"), 0);
	out_master_.setv("SETTLE_DT",	master_.getv("SETTLE_DT"));
	out_master_.setv("EPS_DT",	master_.getv("EPS_DT"));
	out_master_.setv("EXPIRE_DT",	master_.getv("EXPIRE_DT"));
	out_master_.setv("NORMAL_VAL_CUR",	master_.getv("NORMAL_VAL_CUR"));
	out_master_.setv("ORDER_SZ",	master_.getv("ORDER_SZ"), 0);
	out_master_.setv("BID_UNIT",	master_.getv("BID_UNIT"), 0);
	out_master_.setv("ASK_UNIT",	master_.getv("ASK_UNIT"), 0);
	out_master_.setv("TICK_SZ_TYPE",master_.getv("TICK_SZ_TYPE"));
	out_master_.setv("SPLIT_RATE",	master_.getv("SPLIT_RATE"), 3);
	out_master_.setv("SPLIT_DT",	master_.getv("SPLIT_DT"));
	out_master_.setv("EX_DIVIDEND_DT",	master_.getv("EX_DIVIDEND_DT"));
	out_master_.setv("REFERENCE_CD",master_.getv("REFERENCE_CD"));

	//out_master_.setv("MARKET_IN",	master_.getv("MARKET_IN"));
	out_master_.setv("MARKET_IN", (char *)" ");

	out_master_.setv("SELLONLY_GB",	(char *)" ");
	out_master_.setv("VCM_GB",	(char *)" ");
	out_master_.setv("CAS_GB",	(char *)" ");

	out_master_.setv("FILLER", (char *)" ");
	out_master_.setv("FF",	0xff);

	int rc = 0;
	if (writelock_)
		rc = this->NFSWRITE().writelock(sambuf, this->outfile_recsize_);
	else
		rc = this->NFSWRITE().write(sambuf, this->outfile_recsize_);
	if (rc <= 0)
	{
		//ACE_DEBUG((LM_ERROR,
		//		"[%D][%P][%t][%N.%l] nfs write error. [%d][%.1024s]\n",
		//		this->outfile_recsize_,
		//		sam_buf));
		EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_ERROR,
							EVENTNOTIFY_ALARM_YES,
							"OUTFILE WRITE ERROR");
		return -1;
	}

	if (sambuf) delete sambuf;

	master_.set("SEND_MASTER_COUNT", master_.get("SEND_MASTER_COUNT") + 1);

	return 0;
}

double RAFR_JAPANEQUITYTREP2MAST::process_pct_chng (double base_prc, double net_chng)
{
	double  pct_chng = 0.0;

	if (net_chng == 0)
	{
		pct_chng = 0;
	}
	else
	{
		if (base_prc > 0)
			pct_chng = (net_chng / base_prc) * 100.0;
		else
			pct_chng = 0;
	}

	return pct_chng;
}

double RAFR_JAPANEQUITYTREP2MAST::process_net_chng (double base_prc, double trd_prc)
{
	double  net_chng = 0.0;

	if (base_prc == 0)
	{
		net_chng = 0;
	}
	else
	{
		if (trd_prc > 0)
			net_chng = trd_prc - base_prc;
		else
			net_chng = 0;
	}

	return net_chng;
}


int RAFR_JAPANEQUITYTREP2MAST::process_sise_outfile ()
{
	// ACE_DEBUG((LM_DEBUG, "[%D][%P][%t][%N.%l] process_sise_outfile()\n"));

	if (this->outfile_.length() == 0) return 0;


	char *dateTime = getDateTime();
	out_sise_.setv("TRANS_TM", dateTime);
	free(dateTime);

	out_sise_.leftjustify((char*)"RIC_CD", master_.getv("RIC_CD"));
	out_sise_.leftjustify((char*)"SYMBOL_CD", master_.getv("SYMBOL_CD"));
	
	ItemValue trd_dt = master_.getv("TRD_DT");		// FID(16)
	ItemValue sal_tm = master_.getv("SAL_TM");		// FID(379)
	ItemValue local_tm = master_.getv("LOCAL_TM");		// FID(18)

	if (master_.getv("SAL_TM").compare("      ") == 0
	 && master_.getv("LOCAL_TM").compare("      ") == 0)
	{
		memset(omsg_->LOCAL_TM, 0x20, sizeof(omsg_->LOCAL_TM));
		memset(omsg_->KOR_TM, 0x20, sizeof(omsg_->KOR_TM));		
	}
	else
	{
		int localtm = master_.getv("SAL_TM").ival();
		if(localtm == 0)
			localtm = master_.getv("LOCAL_TM").ival();
		out_sise_.setv2("LOCAL_TM", set_time(localtm, gmt_second_));
		out_sise_.setv2("KOR_TM", set_time(localtm, 32400));	
	}

	// OPEN, HIGH, LOW
	memcpy(omsg_->OPEN_PRC, pmaster_->OPEN_PRC, omsg_->TRD_PRC-omsg_->OPEN_PRC);
	// TRD_PRC
	memcpy(omsg_->TRD_PRC, pmaster_->TRD_PRC, sizeof(omsg_->TRD_PRC));

	///////////////////////////////////////////////////////////////////////
	// JAPAN : 대비/등락률 처리- 값이 0이면 계산홤.
	///////////////////////////////////////////////////////////////////////

	double d_base_prc = master_.getv(1465).dval();
	double d_trd_prc = master_.getv(6).dval();
	double d_net_chng = master_.getv(11).dval();
	double d_pct_chng = master_.getv(56).dval();
	if(d_net_chng == 0)
	{
		d_net_chng = process_net_chng(d_base_prc, d_trd_prc);
		d_pct_chng = process_pct_chng(d_base_prc, d_net_chng);
	}

	master_.setv(11, d_net_chng, trd_unit_);
	master_.setv(56, d_pct_chng, 4);
	///////////////////////////////////////////////////////////////////////

	if (d_net_chng == 0 || d_trd_prc == 0) 
	{ 
		out_sise_.setv("NET_CHNG_SIGN", (char *)"3");
	} 
	else if (d_net_chng > 0) 
	{
		if (d_trd_prc >= master_.getv("UPLIMIT").dval()
		 && master_.getv("UPLIMIT").dval() > 0) 
		{
			out_sise_.setv("NET_CHNG_SIGN", (char *)"1");
		} 
		else 
		{
			out_sise_.setv("NET_CHNG_SIGN", (char *)"2");
		}
	} 
	else if (d_net_chng < 0) 
	{
		if (d_trd_prc <= master_.getv("DNLIMIT").dval()
		 && master_.getv("DNLIMIT").dval() > 0)
		{
			out_sise_.setv("NET_CHNG_SIGN", (char *)"4");
		} 
		else 
		{
			out_sise_.setv("NET_CHNG_SIGN", (char *)"5");
		}
	} 
	else 
	{
		out_sise_.setv("NET_CHNG_SIGN", (char *)"3");
	}

	out_sise_.setv("NET_CHNG", master_.getv(11), trd_unit_);
	out_sise_.setv("PCT_CHNG", master_.getv(56), 4);

	// BID_PRC,SIZE
	memcpy(omsg_->BID_PRC, pmaster_->BID_PRC, omsg_->ASK_PRC-omsg_->BID_PRC);
	// ASK_PRC,SIZE
	memcpy(omsg_->ASK_PRC, pmaster_->ASK_PRC, omsg_->TRD_VOL-omsg_->ASK_PRC);
	// TRD_VOL, SVOL
	memcpy(omsg_->TRD_VOL, pmaster_->TRD_VOL, omsg_->SAMT-omsg_->TRD_VOL);

	double samt = master_.getv("SAMT").dval() / 1000.0;
	out_sise_.setv("SAMT", samt, 0 );
	//out_sise_.setv("SAMT", master_.getv("SAMT").dval() / 1000.0, 0 );

	if (d_trd_prc <= master_.getv(22).dval()) 
	{
		out_sise_.setv("TRAND_GB", (char *)"2");
	} 
	else 
	{
		out_sise_.setv("TRAND_GB", (char *)"1");
	}
	
	ItemValue open_prc_tm = master_.getv("OPEN_PRC_TM");
	ItemValue high_prc_tm = master_.getv("HIGH_PRC_TM");
	ItemValue low_prc_tm = master_.getv("LOW_PRC_TM");

	if (trd_dt.compare("        ") == 0
	 || open_prc_tm.compare("      ") == 0)
	{
		//out_sise_.setv("OPEN_PRC_TM", ItemValue(""));
		out_sise_.setv("OPEN_PRC_TM", (char *)" ");
	}
	else
	{
		out_sise_.setv2("OPEN_PRC_TM", set_time(open_prc_tm.ival(), gmt_second_));
	}

	if (trd_dt.compare("        ") == 0
	 || high_prc_tm.compare("      ") == 0)
	{
		//out_sise_.setv("HIGH_PRC_TM", ItemValue(""));
		out_sise_.setv("HIGH_PRC_TM", (char *)" ");
	}
	else
	{
		out_sise_.setv2("HIGH_PRC_TM", set_time(high_prc_tm.ival(), gmt_second_));
	}

	if (trd_dt.compare("        ") == 0
	 || low_prc_tm.compare("      ") == 0)
	{
		//out_sise_.setv("LOW_PRC_TM", ItemValue(""));
		out_sise_.setv("LOW_PRC_TM", (char *)" ");
	}
	else
	{
		out_sise_.setv2("LOW_PRC_TM", set_time(low_prc_tm.ival(), gmt_second_));
	}

	int rc = 0;
	if (writelock_)
		rc = this->NFSWRITE().writelock(sambuf_, this->outfile_recsize_);
	else
		rc = this->NFSWRITE().write(sambuf_, this->outfile_recsize_);

	if (rc <= 0)
	{
		EVENTNOTIFY::instance()->evtNotify(EVENTNOTIFY_LEVEL_ERROR,
							EVENTNOTIFY_ALARM_YES,
							"OUTFILE WRITE ERROR");
		return -1;
	}

	return 0;
}

int RAFR_JAPANEQUITYTREP2MAST::close (void)
{
	return 0;
}

ACE_END_VERSIONED_NAMESPACE_DECL

#endif /* RAFR_JAPANEQUITYTREP2MAST_CPP_ */
