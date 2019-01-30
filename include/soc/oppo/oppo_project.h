/* 
 *
 * yixue.ge add for oppo project
 *
 *
 */
#ifndef _OPPO_PROJECT_H_
#define _OPPO_PROJECT_H_

enum{
        HW_VERSION__UNKNOWN,
        HW_VERSION__10,
        HW_VERSION__11,
        HW_VERSION__12,
        HW_VERSION__13,
        HW_VERSION__14,
        HW_VERSION__15,
        HW_VERSION__16,
        HW_VERSION__17,
        HW_VERSION__18,
        HW_VERSION__19,
        HW_VERSION__20,
        HW_VERSION__21,
        HW_VERSION__22,
};


enum{
        RF_VERSION__UNKNOWN,
        RF_VERSION__11,
        RF_VERSION__12,
        RF_VERSION__13,
        RF_VERSION__14,
        RF_VERSION__15,
        RF_VERSION__16,
        RF_VERSION__17,
        RF_VERSION__18,
        RF_VERSION__19,
        RF_VERSION__1A,
};


#define GET_PCB_VERSION() (get_PCB_Version())
#define GET_PCB_VERSION_STRING() (get_PCB_Version_String())

#define GET_MODEM_VERSION() (get_Modem_Version())
#define GET_OPERATOR_VERSION() (get_Operator_Version())



enum OPPO_PROJECT {
        OPPO_UNKOWN = 0,
        OPPO_17107 = 17107,
        OPPO_17108 = 17108,
        OPPO_17109 = 17109,
        OPPO_17127 = 17127,
        OPPO_17128 = 17128,
};

enum OPPO_OPERATOR {
        OPERATOR_UNKOWN                     = 0,
        OPERATOR_OPEN_MARKET          = 1,
        OPERATOR_CHINA_MOBILE             = 2,
        OPERATOR_CHINA_UNICOM              = 3,
        OPERATOR_CHINA_TELECOM                = 4,
        OPERATOR_FOREIGN                             = 5,
/*#ifdef VENDOR_EDIT*/
/*TongJing.Shi@EXP.DataComm.Phone, 2014.04.18, Add for exp just 3G no 4G*/
        OPERATOR_FOREIGN_WCDMA         = 6,   /*qifeng.liu 2014.08.07 FOR MAC*/
        OPERATOR_FOREIGN_RESERVED   = 7,    /*shaoming 2014/10/04 add for 14085's dual sim version*/
        OPERATOR_ALL_CHINA_CARRIER    = 8,   /*instead of TELECOM CARRIER because of history Tong.han@Bsp.Group.Tp add for all china carrier phone, 2015/03/23*/
        OPERATOR_ALL_CHINA_CARRIER_MOBILE = 9,    /*rendong.shi@Bsp.Group.Tp add for all china carrier MOBILE phone, 2016/01/07*/
        OPERATOR_ALL_CHINA_CARRIER_UNICOM = 10,    /*rendong.shi@Bsp.Group.Tp add for all china carrier UNICOM  phone, 2016/01/07*/
};

typedef enum OPPO_PROJECT OPPO_PROJECT;
#define OCPCOUNTMAX 4
typedef struct
{
        unsigned int                  nproject;
        unsigned char                 nmodem;
        unsigned char                 noperator;
        unsigned char                 npcbversion;
        unsigned char                 noppobootmode;
        unsigned char                 npmicocp[OCPCOUNTMAX];
} ProjectInfoCDTType;

#ifdef CONFIG_OPPO_COMMON_SOFT
void init_project_version(void);
unsigned int get_project(void);
unsigned int is_project(OPPO_PROJECT project);
unsigned char get_PCB_Version(void);
unsigned char get_Modem_Version(void);
unsigned char get_Operator_Version(void);
#else
unsigned int init_project_version(void) { return 0;}
unsigned int get_project(void) { return 0;}
unsigned int is_project(OPPO_PROJECT project) { return 0;}
unsigned char get_PCB_Version(void) { return 0;}
unsigned char get_Modem_Version(void) { return 0;}
unsigned char get_Operator_Version(void) { return 0;}
#endif
#endif  /*_OPPO_PROJECT_H_*/
