
#include "basicio.h"
#include "string.h"


/*
 * デジタルIO
 */


//pinNO=0..19 mode=INPUT/INPUT_PULLUP/OUTPUT
bool_t pinMode(uint8_t pinNo, PINMODES mode) {
    if (pinNo > 20) return FALSE;

    if (mode == OUTPUT) {
        //OUTPUTモードにする
        vAHI_DioSetDirection(0, 1UL << pinNo);
    } else {
        //INPUTモードにする
        vAHI_DioSetDirection(1UL << pinNo, 0);
        if (mode == INPUT_PULLUP) {
            //プルアップ有効
            vAHI_DioSetPullup(1UL << pinNo, 0);
        } else {
            //プルアップ無効
            vAHI_DioSetPullup(0, 1UL << pinNo);
        }
    }
    return TRUE;
}

//pinNO=0..19 value=LOW/HIGH
//事前にpinModeでOUTPUTを設定しておく
bool_t digitalWrite(uint8_t pinNo, uint8_t value) {
    if (pinNo > 20) return FALSE;

    if (value == LOW) {
        vAHI_DioSetOutput(0, 1UL << pinNo);
    } else {
        vAHI_DioSetOutput(1UL << pinNo, 0);
    }
    return TRUE;
}


/*
 * デジタルIO(割り込み)
 */

//割り込みルーチンのポインタを保持
void (*dioCallbackFunctions[20])();

//pinNO=0..19 funcは引数を持たない関数 mode=RISING(立ち上がり)/FALLING(立ち下がり)/DISABLE
//一つのピンに一つの関数しか登録できない
bool_t attachDioCallback(uint8_t pinNo, void (*func)(), INTERRUPTIONEDGES mode) {
    if (mode == DISABLE) {
        return detachDioCallback(pinNo);
    }

    if (pinNo > 19) return FALSE;

    //処理ルーチンのポインタを登録
    dioCallbackFunctions[pinNo] = func;

    //割り込みを有効にする
    vAHI_DioInterruptEnable(1UL << pinNo, 0);

    if (mode == RISING) {
        //立ち上がりで発生
        vAHI_DioInterruptEdge(1UL << pinNo, 0);
    } else {
        //立下りで発生
        vAHI_DioInterruptEdge(0, 1UL << pinNo); 
    }
    return TRUE;
}

//pinNO=0..19
bool_t detachDioCallback(uint8_t pinNo) {
    if (pinNo > 19) return FALSE;

    //処理ルーチンのポインタを削除
    dioCallbackFunctions[pinNo] = NULL;

    //割り込みを無効にする
    vAHI_DioInterruptEnable(0, 1UL << pinNo);
    return TRUE;
}


/*
 * タイマー／ＰＷＭ
 */

//割り込みルーチンのポインタを保持
void (*timerCallbackFunctions[5])();

//タイマーコンテキストを保持
tsTimerContext sTimerApp[5];

//vAHI_TimerFineGrainDIOControl()の値を保持（DIOピンを汎用かPWMで使うか）
uint8_t timerFineGrainDIOControlValue;

const uint8_t timerDeviceIndices[5] = {E_AHI_DEVICE_TIMER0,E_AHI_DEVICE_TIMER1,E_AHI_DEVICE_TIMER2,E_AHI_DEVICE_TIMER3,E_AHI_DEVICE_TIMER4};

//PWM
//timerNo = 0..4  duty = 0..1024
//Ct = 16000000 / (2^prescale) / hz が65535を超えないこと
//startTimerで開始します
bool_t attachTimerPWM(uint8_t timerNo, uint16_t hz, uint8_t prescale, uint16_t duty) {
    if (timerNo > 4) return FALSE;

    uint8_t b = 4 << timerNo;
    if ((timerFineGrainDIOControlValue & b) != 0) {
        //DIOを汎用からタイマー用に切り替える
        timerFineGrainDIOControlValue &= b ^ 255;
        vAHI_TimerFineGrainDIOControl(timerFineGrainDIOControlValue);
    }

    memset(&sTimerApp[timerNo], 0, sizeof(tsTimerContext));
    sTimerApp[timerNo].u8Device = timerDeviceIndices[timerNo];
    sTimerApp[timerNo].u16Hz = hz;
    sTimerApp[timerNo].u8PreScale = prescale;
    sTimerApp[timerNo].u16duty = duty;
    sTimerApp[timerNo].bPWMout = TRUE;
    sTimerApp[timerNo].bDisableInt = TRUE; // no interrupt (for CPU save)
    vTimerConfig(&sTimerApp[timerNo]);
    return TRUE;
}

//timerNo = 0..4
//Ct = 16000000 / (2^prescale) / hz が65535を超えないこと
//startTimerで開始します
bool_t attachTimerCallback(uint8_t timerNo, uint16_t hz, uint8_t prescale, void (*func)()) {
    if (timerNo > 4) return FALSE;

    uint8_t b = 4 << timerNo;
    if ((timerFineGrainDIOControlValue & b) == 0) {
        //DIOをタイマー用から汎用に切り替える
        timerFineGrainDIOControlValue |= b;
        vAHI_TimerFineGrainDIOControl(timerFineGrainDIOControlValue);
    }

    memset(&sTimerApp[timerNo], 0, sizeof(tsTimerContext));
    sTimerApp[timerNo].u8Device = timerDeviceIndices[timerNo];
    sTimerApp[timerNo].u16Hz = hz;
    sTimerApp[timerNo].u8PreScale = prescale;

    timerCallbackFunctions[timerNo] = func;

    vTimerConfig(&sTimerApp[timerNo]); // initialize
    return TRUE;
}

//timerNo = 0..4
bool_t detachTimer(uint8_t timerNo) {
    if (timerNo > 4) return FALSE;
    
    vTimerStop(&sTimerApp[timerNo]);
    vTimerDisable(&sTimerApp[timerNo]);

    timerCallbackFunctions[timerNo] = NULL;

    if (sTimerApp[timerNo].bPWMout == TRUE) {
        //PWMに使用した後はDIOをタイマー用から汎用に切り替える
        timerFineGrainDIOControlValue |= 4 << timerNo;
        vAHI_TimerFineGrainDIOControl(timerFineGrainDIOControlValue);
    }
    return TRUE;
}


/*
 * スリープ
 */

//DIOピンによるウェイクアップ pinNO=0..19 mode=RISING(立ち上がり)/FALLING(立ち下がり)/DISABLE
//事前にpinModeをINPUTに設定しておくこと（スリープ中はINPUT_PULLUPは使えないので自前で準備すること）
bool_t setDioWake(uint8_t pinNo, INTERRUPTIONEDGES mode) {
    if (pinNo > 19) return FALSE;

    (void)u32AHI_DioInterruptStatus(); // clear interrupt register
    if (mode != DISABLE) {
        vAHI_DioWakeEnable(1UL << pinNo, 0); // enable ports
        if (mode == RISING) {
            vAHI_DioWakeEdge(1UL << pinNo, 0); // set edge (rising)
        } else {
            vAHI_DioWakeEdge(0, 1UL << pinNo); // set edge (falling)
        }
    } else {
        vAHI_DioWakeEnable(0, 1UL << pinNo);
    }
    return TRUE;
}


/*
 * シリアル
 */

#ifdef USE_SERIAL0

// FIFOキューや出力用の定義
tsFILE sUartStream0;
tsSerialPortSetup sUartPort0;

// 送信FIFOバッファ
#ifdef SERIAL0_TX_BUFFER_SIZE
uint8_t au8SerialTxBuffer0[SERIAL0_TX_BUFFER_SIZE]; 
#else
uint8_t au8SerialTxBuffer0[96];    //デフォルトサイズ
#endif

// 受信FIFOバッファ
#ifdef SERIAL0_RX_BUFFER_SIZE
uint8_t au8SerialRxBuffer0[SERIAL0_RX_BUFFER_SIZE]; 
#else
uint8_t au8SerialRxBuffer0[32];    //デフォルトサイズ
#endif

#endif //USE_SERIAL0


#ifdef USE_SERIAL1

tsFILE sUartStream1;
tsSerialPortSetup sUartPort1;

// 送信FIFOバッファ
#ifdef SERIAL1_TX_BUFFER_SIZE
uint8_t au8SerialTxBuffer1[SERIAL1_TX_BUFFER_SIZE]; 
#else
uint8_t au8SerialTxBuffer1[96];    //デフォルトサイズ
#endif

// 受信FIFOバッファ
#ifdef SERIAL1_RX_BUFFER_SIZE
uint8_t au8SerialRxBuffer1[SERIAL1_RX_BUFFER_SIZE]; 
#else
uint8_t au8SerialRxBuffer1[32];    //デフォルトサイズ
#endif

#endif //USE_SERAIL1


#if defined(USE_SERIAL0) || defined(USE_SERIAL1)
//Serial0,1共通関数を定義

bool_t Serial_write(uint8_t u8SerialPort, uint8_t *pu8Data, uint8_t length)
{
    bool_t bResult = TRUE;

    while(length > 0)
    {
        if (SERIAL_bTxChar(u8SerialPort, *pu8Data))
        {
            pu8Data++;
        }
        else
        {
            bResult = FALSE;
            break;
        }
        length--;
    }
    return bResult;
}

#endif //USE_SERIAL0 || USE_SERIAL1


#ifdef USE_SERIAL0

//定数 BAUDRATE_115200 などを渡すこと
bool_t Serial0_init(BAUDRATES baudRate) {
    if ((baudRate & 0x80000000) == 0) return FALSE;

    sUartPort0.pu8SerialRxQueueBuffer = au8SerialRxBuffer0;
    sUartPort0.pu8SerialTxQueueBuffer = au8SerialTxBuffer0;
    sUartPort0.u32BaudRate = baudRate;
    sUartPort0.u16AHI_UART_RTS_LOW = 0xffff;
    sUartPort0.u16AHI_UART_RTS_HIGH = 0xffff;
    sUartPort0.u16SerialRxQueueSize = sizeof(au8SerialRxBuffer0);
    sUartPort0.u16SerialTxQueueSize = sizeof(au8SerialTxBuffer0);
    sUartPort0.u8SerialPort = E_AHI_UART_0;
    sUartPort0.u8RX_FIFO_LEVEL = E_AHI_UART_FIFO_LEVEL_1;
    SERIAL_vInit(&sUartPort0);

    sUartStream0.bPutChar = SERIAL_bTxChar;
    sUartStream0.u8Device = E_AHI_UART_0;
    return TRUE;
}

//debugLevel=0..5 (0:デバッグ出力無し)
bool_t Serial0_forDebug(uint8_t debugLevel) {
    if (debugLevel > 5) return FALSE;

    ToCoNet_vDebugInit(&sUartStream0);
	ToCoNet_vDebugLevel(debugLevel);
    return TRUE;
}
#endif //USE_SERIAL0

#ifdef USE_SERIAL1

//定数 BAUDRATE_115200 などを渡すこと
bool_t Serial1_init(BAUDRATES baudRate) {
    if ((baudRate & 0x80000000) == 0) return FALSE;

    sUartPort1.pu8SerialRxQueueBuffer = au8SerialRxBuffer1;
    sUartPort1.pu8SerialTxQueueBuffer = au8SerialTxBuffer1;
    sUartPort1.u32BaudRate = baudRate;
    sUartPort1.u16AHI_UART_RTS_LOW = 0xffff;
    sUartPort1.u16AHI_UART_RTS_HIGH = 0xffff;
    sUartPort1.u16SerialRxQueueSize = sizeof(au8SerialRxBuffer1);
    sUartPort1.u16SerialTxQueueSize = sizeof(au8SerialTxBuffer1);
    sUartPort1.u8SerialPort = E_AHI_UART_1;
    sUartPort1.u8RX_FIFO_LEVEL = E_AHI_UART_FIFO_LEVEL_1;
    SERIAL_vInit(&sUartPort1);

    sUartStream1.bPutChar = SERIAL_bTxChar;
    sUartStream1.u8Device = E_AHI_UART_1;
    return TRUE;
}

//debugLevel=0..5 (0:デバッグ出力無し)
bool_t Serial1_forDebug(uint8_t debugLevel) {
    if (debugLevel > 5) return FALSE;

    ToCoNet_vDebugInit(&sUartStream1);
	ToCoNet_vDebugLevel(debugLevel);
    return TRUE;
}

#endif //USE_SERAIL1


/*
 * ＡＤＣ
 */


//割り込みルーチンのポインタを保持
static void (*adcCallbackFunction)(uint16,int16_t);

//attachAdcCallback()の設定を保持
static bool_t adcIsContinuous;
static bool_t adcIsRange2;
static ADCSOURCES adcLastSource;

//sample = サンプリング数, clock = ADCモジュールのクロック(500KHZが推奨)
//AD変換時間は (サンプリング数 x 3 + 14)クロック 
void enableAdcModule(ADCSAMPLES sample, ADCCLOCKS clock) {
    if (!bAHI_APRegulatorEnabled()) {
        //アナログ部の電源投入
        vAHI_ApConfigure(E_AHI_AP_REGULATOR_ENABLE, // DISABLE にするとアナログ部の電源断
            E_AHI_AP_INT_ENABLE,    // 割り込み
            sample,                 // サンプル数 2,4,6,8 が選択可能
            clock,                  // 周波数 250K/500K/1M/2M
            E_AHI_AP_INTREF);

        while(!bAHI_APRegulatorEnabled()); // 安定するまで待つ（一瞬）
    }
}

void disableAdcModule() {
    if (bAHI_APRegulatorEnabled()) {
        vAHI_ApConfigure(E_AHI_AP_REGULATOR_DISABLE,    //OFF
            E_AHI_AP_INT_DISABLE,                       //OFF
            ADC_SAMPLE_4,
            ADC_CLOCK_500KHZ,
            E_AHI_AP_INTREF);
    }
}

//contiuous=TRUE:連続,FALSE:1SHOT  range2=TRUE:0～Vref[V],FALSE:0～2*Vref[V] *Vrefは約1.235V
//SOURCE_ADC3,4はそれぞれDIO0,1と共用
void attachAdcCallback(bool_t continuous, bool_t range2, ADCSOURCES source, void (*func)(uint16_t rawData, int16_t adcResult)) {

    switch (source) {
    case SOURCE_ADC3:
        pinMode(0, INPUT);
        break;
    case SOURCE_ADC4:
        pinMode(1, INPUT);
        break;
    case SOURCE_TEMP:
        range2 = FALSE;
        break;
    case SOURCE_VOLT:
        range2 = TRUE;
    }

    adcIsContinuous = continuous;
    adcIsRange2 = range2;
    adcLastSource = source;
    adcCallbackFunction = func;

    vAHI_AdcEnable(continuous, range2, source);
    vAHI_AdcStartSample(); // ADC開始
}

void detachAdcCallback()  {
    adcCallbackFunction = NULL;

    if (adcIsContinuous) {
        //コンティニュアスモードの場合は停止
        vAHI_AdcDisable();
    }
}


/*
 * Ｉ２Ｃ
 */

static bool_t bSMBusWait(void)
{
	while(bAHI_SiMasterPollTransferInProgress()); /* busy wait */
	if (bAHI_SiMasterPollArbitrationLost() | bAHI_SiMasterCheckRxNack())	{
		/* release bus & abort */
		vAHI_SiMasterSetCmdReg(E_AHI_SI_NO_START_BIT,
						 E_AHI_SI_STOP_BIT,
						 E_AHI_SI_NO_SLAVE_READ,
						 E_AHI_SI_SLAVE_WRITE,
						 E_AHI_SI_SEND_ACK,
						 E_AHI_SI_NO_IRQ_ACK);
        return FALSE;
	}
	return TRUE;
}

//I2Cで指定アドレスにコマンドとデータを書き込む。データが無いときはpu8Data=NULL,u8Length=0とする
bool_t I2C_write(uint8_t u8Address, uint8_t u8Command, const uint8* pu8Data, uint8_t u8Length)
{
	bool_t bCommandSent = FALSE;
	/* Send address with write bit set */
	vAHI_SiMasterWriteSlaveAddr(u8Address, E_AHI_SI_SLAVE_RW_SET);
	vAHI_SiMasterSetCmdReg(E_AHI_SI_START_BIT,
					 E_AHI_SI_NO_STOP_BIT,
					 E_AHI_SI_NO_SLAVE_READ,
					 E_AHI_SI_SLAVE_WRITE,
					 E_AHI_SI_SEND_ACK,
					 E_AHI_SI_NO_IRQ_ACK);//) return FALSE;
	if(!bSMBusWait()) return(FALSE);
	while(bCommandSent == FALSE || u8Length > 0){
		if(!bCommandSent){
			/* Send command byte */
			vAHI_SiMasterWriteData8(u8Command);
			bCommandSent = TRUE;
		} else {
			u8Length--;
			/* Send data byte */
			vAHI_SiMasterWriteData8(*pu8Data++);
		}
		/*
		 * If its the last byte to be sent, send with
		 * stop sequence set
		 */
		if(u8Length == 0){
			vAHI_SiMasterSetCmdReg(E_AHI_SI_NO_START_BIT,
							 E_AHI_SI_STOP_BIT,
							 E_AHI_SI_NO_SLAVE_READ,
							 E_AHI_SI_SLAVE_WRITE,
							 E_AHI_SI_SEND_ACK,
							 E_AHI_SI_NO_IRQ_ACK);//) return FALSE;

		} else {
			vAHI_SiMasterSetCmdReg(E_AHI_SI_NO_START_BIT,
							 E_AHI_SI_NO_STOP_BIT,
							 E_AHI_SI_NO_SLAVE_READ,
							 E_AHI_SI_SLAVE_WRITE,
							 E_AHI_SI_SEND_ACK,
							 E_AHI_SI_NO_IRQ_ACK);//) return FALSE;
		}
		if(!bSMBusWait()) return(FALSE);
	}
	return(TRUE);
}

//I2Cで指定アドレスからデータを読み出します
bool_t I2C_read(uint8_t u8Address, uint8* pu8Data, uint8_t u8Length)
{
	/* Send address with write bit set */
	vAHI_SiMasterWriteSlaveAddr(u8Address, !E_AHI_SI_SLAVE_RW_SET);
	vAHI_SiMasterSetCmdReg(E_AHI_SI_START_BIT,
					 E_AHI_SI_NO_STOP_BIT,
					 E_AHI_SI_NO_SLAVE_READ,
					 E_AHI_SI_SLAVE_WRITE,
					 E_AHI_SI_SEND_ACK,
					 E_AHI_SI_NO_IRQ_ACK);//) return FALSE;
	if(!bSMBusWait()) return(FALSE);
	while(u8Length > 0){
		u8Length--;
		/*
		 * If its the last byte to be sent, send with
		 * stop sequence set
		 */
		if(u8Length == 0){
			vAHI_SiMasterSetCmdReg(E_AHI_SI_NO_START_BIT,
							 E_AHI_SI_STOP_BIT,
							 E_AHI_SI_SLAVE_READ,
							 E_AHI_SI_NO_SLAVE_WRITE,
							 E_AHI_SI_SEND_NACK,
							 E_AHI_SI_NO_IRQ_ACK);//) return FALSE;
		} else {
			vAHI_SiMasterSetCmdReg(E_AHI_SI_NO_START_BIT,
							 E_AHI_SI_NO_STOP_BIT,
							 E_AHI_SI_SLAVE_READ,
							 E_AHI_SI_NO_SLAVE_WRITE,
							 E_AHI_SI_SEND_ACK,
							 E_AHI_SI_NO_IRQ_ACK);//) return FALSE;
		}
		while(bAHI_SiMasterPollTransferInProgress()); /* busy wait */
		*pu8Data++ = u8AHI_SiMasterReadData8();
	}
	return(TRUE);
}

//I2Cで指定アドレスのコマンドからデータを読み出します
bool_t I2C_commandRead(uint8_t u8Address, uint8_t u8Command, uint8* pu8Data, uint8_t u8Length) {
    if (!I2C_write(u8Address, u8Command, NULL, 0)) return FALSE;
    if (!I2C_read(u8Address, pu8Data, u8Length)) return FALSE;
    return TRUE;
}

//I2Cで指定アドレスにコマンドと1バイトのデータを書き込む
bool_t I2C_writeByte(uint8_t u8Address, uint8_t u8Command, uint8_t u8Data) {
    return I2C_write(u8Address, u8Command, &u8Data, 1);
}

//I2Cで指定アドレスからデータを1バイト読み出します。失敗で-1
int16_t I2C_readByte(uint8_t u8Address) {
    uint8_t data;
    if (!I2C_read(u8Address, &data, 1)) return -1;
    return (int16_t)data;
}


/*
 * ＳＰＩ
 */

//SPIマスターを有効にする
//u8NumSlaves=1..3
//使用するピン CLK:DO0(ｼﾙｸC), MISO(in):DO1(ｼﾙｸI), MOSI(out):DIO18
//最初のスレーブはSS(CSB)にDIO19を使うが、1つのスレーブの場合はスレーブのSS(CSB)をGNDに落として常時選択でもOK
bool_t SPI_enable(uint8_t u8NumSlaves, bool_t bLsbFirst, SPIMODES u8Mode, SPICLOCKS u8Divider) {
    if (u8NumSlaves == 0 || u8NumSlaves > 3) return FALSE;

    bool_t polarity, phase;
    switch (u8Mode) {
    case SPI_MODE_0: polarity=FALSE; phase=FALSE; break;
    case SPI_MODE_1: polarity=FALSE; phase=TRUE; break;
    case SPI_MODE_2: polarity=TRUE; phase=FALSE; break;
    case SPI_MODE_3: polarity=TRUE; phase=TRUE; break;
    default: return FALSE;
    }

    vAHI_SpiConfigure(u8NumSlaves - 1,
        bLsbFirst,                  //bool_t bLsbFirst,
        polarity,                   //bool_t bPolarity,
        phase,                      //bool_t bPhase,
        u8Divider,                  //uint8 u8ClockDivider,
        E_AHI_SPIM_INT_DISABLE,     //bool_t bInterruptEnable,
        E_AHI_SPIM_AUTOSLAVE_DSABL  //bool_t bAutoSlaveSelect
    );
    return TRUE;
}

//slaveNo=1または2のSSピンを変更する。SPI_selectSave()に適用
//TRUEでslaveNo=1または2のSSピンが、DIO0->DIO14, DIO1->DIO15に変更できる
bool_t SPI_selectPin(uint8_t slaveNo, bool_t bSecondPin) {
    if (slaveNo != 1 && slaveNo != 2) return FALSE;
    vAHI_SpiSelSetLocation(slaveNo, bSecondPin);
    return TRUE;
}

//SPIスレーブ 0..2を選択。その他の値で無選択となる
//具体的には0..2で次のDIO(SSピン)がLになる DIO19,DIO0*,DIO1*  *SPI_selectPin()で変更可能
void SPI_selectSlave(int8_t slaveNo) {
    if (slaveNo >= 0 && slaveNo <= 2) {
        vAHI_SpiSelect(slaveNo + 1);
    } else {
        vAHI_SpiSelect(0);
    }
}

bool_t SPI_write(uint32_t u32Data, uint8_t u8BitLength) {
    if (u8BitLength == 0 || u8BitLength > 32) return FALSE;
    vAHI_SpiStartTransfer(u8BitLength - 1, u32Data);
    while(bAHI_SpiPollBusy());
    return TRUE;
}

void SPI_writeByte(uint8_t u8Data) {
    vAHI_SpiStartTransfer(7, u8Data);
    while(bAHI_SpiPollBusy());
}


/*
 * 隠ぺいされた基本関数
 */

//ユーザーが準備する関数
extern void setup(bool_t warmWake, uint32_t dioWakeStatus);
extern void loop(EVENTS event);

// イベントハンドラ
static void vProcessEvCore(tsEvent *pEv, teEvent eEvent, uint32_t u32evarg)
{
    //ユーザーが設定した処理ルーチンを呼び出す
    switch (eEvent) {
	case E_EVENT_START_UP:
        loop(EVENT_START_UP);
        break;
    case E_EVENT_TICK_TIMER: 
        loop(EVENT_TICK_TIMER);
        break;
    case E_EVENT_TICK_SECOND:
        loop(EVENT_TICK_SECOND);
        break;
    }
}

//変数や構造体を初期化
void resetVars()
{
    memset(dioCallbackFunctions, 0, sizeof(dioCallbackFunctions));
    memset(timerCallbackFunctions, 0, sizeof(timerCallbackFunctions));
    memset(sTimerApp, 0, sizeof(sTimerApp));
    timerFineGrainDIOControlValue = 0xFF;
    memset(adcCallbackFunction, 0, sizeof(adcCallbackFunction));
}

// 電源オンによるスタート
void cbAppColdStart(bool_t bAfterAhiInit)
{
    static uint32_t dioWakeStatus;

	if (!bAfterAhiInit) {

        //起動原因を調査
        if (u8AHI_WakeTimerFiredStatus()) {
            dioWakeStatus = 0;
        } else {
            dioWakeStatus = u32AHI_DioWakeStatus();
        }

        ToCoNet_REG_MOD_ALL();
	} else {
        //変数や構造体を初期化
        resetVars();

        //イベントハンドラを登録
        ToCoNet_Event_Register_State_Machine(vProcessEvCore);

        //ユーザーの初期化ルーチンを呼び出す
        setup(FALSE, dioWakeStatus);
	}
}

// スリープからの復帰
void cbAppWarmStart(bool_t bAfterAhiInit)
{
    static uint32_t dioWakeStatus;

	if (!bAfterAhiInit) {

        //起動原因を調査
        if (u8AHI_WakeTimerFiredStatus()) {
            dioWakeStatus = 0;
        } else {
            dioWakeStatus = u32AHI_DioWakeStatus();
        }

        ToCoNet_REG_MOD_ALL();
	} else {

        //変数や構造体を初期化
        resetVars();

        //イベントハンドラを登録
        ToCoNet_Event_Register_State_Machine(vProcessEvCore);

        //ユーザーの初期化ルーチンを呼び出す
        setup(TRUE, dioWakeStatus);
	}
}

// ネットワークイベント発生時
void cbToCoNet_vNwkEvent(teEvent eEvent, uint32_t u32arg)
{
}

// パケット受信時
void cbToCoNet_vRxEvent(tsRxDataApp *pRx)
{
}

// パケット送信完了時
void cbToCoNet_vTxEvent(uint8_t u8CbId, uint8_t bStatus)
{
}

// ハードウェア割り込み発生後（遅延呼び出し）
void cbToCoNet_vHwEvent(uint32_t u32DeviceId, uint32_t u32ItemBitmap)
{
    //割り込みに対する処理は通常ここで行う。
    switch (u32DeviceId) {

    case E_AHI_DEVICE_TIMER0:
        //タイマー0割り込み処理ルーチンの呼び出し
        if (timerCallbackFunctions[0] != NULL) {
            (*timerCallbackFunctions[0])();
        }
        break;

    case E_AHI_DEVICE_TIMER1:
        //タイマー1割り込み処理ルーチンの呼び出し
        if (timerCallbackFunctions[1] != NULL) {
            (*timerCallbackFunctions[1])();
        }
        break;

    case E_AHI_DEVICE_TIMER2:
        //タイマー2割り込み処理ルーチンの呼び出し
        if (timerCallbackFunctions[2] != NULL) {
            (*timerCallbackFunctions[2])();
        }
        break;

    case E_AHI_DEVICE_TIMER3:
        //タイマー3割り込み処理ルーチンの呼び出し
        if (timerCallbackFunctions[3] != NULL) {
            (*timerCallbackFunctions[3])();
        }
        break;

    case E_AHI_DEVICE_TIMER4:
        //タイマー4割り込み処理ルーチンの呼び出し
        if (timerCallbackFunctions[4] != NULL) {
            (*timerCallbackFunctions[4])();
        }
        break;

    case E_AHI_DEVICE_SYSCTRL:
        //DIO割り込み処理ルーチンの呼び出し
        _C {
            uint32_t b = 1;
            uint8_t pinNo;
            for(pinNo = 0; pinNo < 20; pinNo++) {
                if ((u32ItemBitmap & b) && dioCallbackFunctions[pinNo] != NULL) {
                    (*dioCallbackFunctions[pinNo])();
                }
                b <<= 1;
            }
        }
        break;

    case E_AHI_DEVICE_ANALOGUE:
        //ADC(完了)割り込みルーチンの呼び出し
        _C {
            if (adcCallbackFunction != NULL) {

                //ADC値の読み出しと単位変換

                uint16_t adcValue = u16AHI_AdcRead(); // 値の読み出し
                int32_t resultValue;

                if (adcLastSource == SOURCE_VOLT) {
                    //自身の電圧は2/3に分圧された値を測定しているので、1023のとき3.705V
                    //3709 = 3.705V / 1023 * 1000 * 1024
                    resultValue = ((int32_t)adcValue * 3709) >> 10; //[mV]

                } else if (adcLastSource == SOURCE_TEMP) {
                    //温度センサーのスペック
                    //730mV@25℃, -1.66mV/℃
                    resultValue = (int32_t)adcValue * 1236;     //x1024[mV]
                    resultValue -= 730 * 1024;              //x1024
                    resultValue *= -771;                    //771=(1/1.66)*1280  x1024x1280
                    resultValue = (resultValue >> 17) + 250; //x10[℃]

                } else {
                    if (adcIsRange2) {
                        //1023 = 2.470V, 2.470V * 1024/1023 = 2472
                        resultValue = (2472 * (int32_t)adcValue) >> 10; //[mV]

                    } else {
                        //1023 = 1.235V, 1.235V * 1024/1023 = 1236
                        resultValue = (1236 * (int32_t)adcValue) >> 10; //[mV]
                    }
                }

                //ユーザー処理ルーチンを呼び出す
                (*adcCallbackFunction)(adcValue, (int16_t)resultValue);
            }
        }
    }
}

uint32_t millisValue = 0;

// ハードウェア割り込み発生時
uint8_t cbToCoNet_u8HwInt(uint32_t u32DeviceId, uint32_t u32ItemBitmap)
{
    //割り込みで最初に呼ばれる。最短で返さないといけない。

    //注)ここで目的の割り込み処理を実行したときだけTRUEを返すようにすること。
    //  常にTRUEを返すと固まる!!

    if (u32DeviceId == E_AHI_DEVICE_TICK_TIMER) {
        //u32AHI_TickTimerRead()がうまく値を返してくれないのでここでカウント
        millisValue += 4;
    }

	return FALSE;//FALSEによりcbToCoNet_vHwEvent()が呼ばれる
}

// メイン
void cbToCoNet_vMain(void)
{
}