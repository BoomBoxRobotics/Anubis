|---------------------------------------------------------------------------------------------------------------------------------------|
|							Anubis RC Controller								|
|							By BoomBox Robotics								|
|---------------------------------------------------------------------------------------------------------------------------------------|
|	The goal for the Anubis RC Controller is to create a community driven, open source RC controller that is built from DIY parts	|
| with all of the features of modern transmitters. We allow our community to fill in the gaps of features wherever they are needed by	|
| making our code open for anybody to modify. The Anubis RC Controller has been designed to transmit via the built in ELRS module, or	|
| via ESP-NOW transmission to another WiFi equipped ESP32 programmed as a receiver. Code for both the Transmitter and Receiver can be	|
| found on our GitHub repo here: https://github.com/BoomBoxRobotics/Anubis								|
|																	|
|---------------------------------------------------------------------------------------------------------------------------------------|
|--------|																|
|Hardware|																|
|--------|																|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Qty		Item						Source									|
|---------------------------------------------------------------------------------------------------------------------------------------|
|1		Hosyond 2.8" ESP32-S3 Display			https://www.amazon.com/dp/B0FKG7WRWV?ref=ppx_yo2ov_dt_b_fed_asin_title	|
|2		Mini Pushbutton Power Switch			https://www.pololu.com/product/2808					|
|2		Radio Master Pocket X5 Gimbals			https://www.fruugo.us/radiomaster-pocket-radio-transmitter-x5-replaceme	|
|								nt-gimbals/p-307085345-687785683					|
|1		MP1584EN Mini Buck Converter DC-DC 3A		https://www.amazon.com/dp/B0DC3N7PMY?ref=ppx_yo2ov_dt_b_fed_asin_title	|
|1		MT3608 DC to DC Step Up Boost Converter		https://www.amazon.com/dp/B0DSZ9G3ZS/ref=sspa_dk_hqp_detail_aax_0?psc=	|
|								1&sp_csd=d2lkZ2V0TmFtZT1zcF9ocXBfc2hhcmVk				|
|1		PCF8575 IO Expansion Board			https://www.amazon.com/dp/B0F2M5Y2LK?ref=ppx_yo2ov_dt_b_fed_asin_title	|
|2		JST MX 1.25mm 4 Pin Male Connector		https://www.amazon.com/dp/B0FSZJTQKQ?ref=ppx_yo2ov_dt_b_fed_asin_title	|
|								&th=1									|
|1		5-Channel Navigation Button Switch Module	https://www.amazon.com/dp/B0D8T4YQ3Y?ref=ppx_yo2ov_dt_b_fed_asin_title	|						|
|1		3,000mAh Lithium Polymer Battery		https://www.amazon.com/YTKavq-3600mAh-Battery-Rechargeable-Connector/dp	|
|								/B08TTQ3PWT/ref=sr_1_1?crid=2CQJRB58QQLGW&dib=eyJ2IjoiMSJ9.n9yeCi6NzEJ3	|
|								hD0ceLljFy6-wyO3nz-gFsvsAqpme6EgbiHp2zFb20yixI0C5z7L7Iluxy7F8jhHPuaoDN-	|
|								m1jLpwyLirwMfkVd8CDKCkqE5sk3rcH1FzgkKNvy-KL_pXCDiUoKF9HLPSeoTeS_GsomF9	|
|								jCH4hHqGF2_50G9CuzLfjvaJsIVych1eobokgUDJd6POM5R6qzidcX8FnSmUEfcdYb1jNF9	|
|								0r7FZIny8Zc.NQ8r1z_ITbAhZCPfBiFbQt_Egn72p50ASO21K2ShytE&dib_tag=se&keyw	|
|								ords=3600mah%2Blipo&qid=1778048277&s=electronics&sprefix=3600mah%2Blipo	|
|								%2Celectronics%2C265&sr=1-1&th=1					|
|1		RadioMaster Pocket ELRS Module			https://radiomasterrc.com/products/x5-replacement-gimbals-for-pocket	|						|
|1		RPSMA to IPEX adapter				https://www.amazon.com/dp/B0931SL6LG?ref=ppx_yo2ov_dt_b_fed_asin_title	|
|---------------------------------------------------------------------------------------------------------------------------------------|
|						||||	HOSYOND 2.8" ESP32-S3 Display	||||						|
|---------------------------------------------------------------------------------------------------------------------------------------|
|								Product Parameters							|
|---------------------------------------------------------------------------------------------------------------------------------------|
|----------------|															|
|ESP32 Parameters|															|
|----------------|															|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Item					Parameters											|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Module					ESP32-S3											|
|CPU					Xtensa LX7 32-bit dual-core processor								|
|Clock rate				240MHz（MAX）											|
|Memory					384KB ROM+512KB SRAM+16KB RTC SRAM+8M internal OPI PSRAM   +16M external SPI Flash (N16R8)	|
|WIFI					2.4GHz, 802.11b/g/n mode									|
|Bluetooth				Bluetooth V5.0 BR/EDR and Bluetooth LE standard							|
|Operation voltage			3.0~3.6(V)											|
|---------------------------------------------------------------------------------------------------------------------------------------|
|-----------------|															|
|Screen Parameters|															|
|-----------------|															|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Item					Parameters											|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Screen Size				2.8 inch											|
|Screen Type				IPS TFT												|
|Screen resolution			240xRGBx320(pixels)										|
|Active Area				43.20(W)x57.60(H)(mm)										|
|Number of pixels			MAX：262K(RGB666)										|
|TYPE：					65K(RGB565)											|
|Drive IC				ILI9341V											|
|Screen interface			4-Line SPI											|
|pixels size				0.153(H)x0.153(mm)										|
|View Angle				ALL 0’CLOCK											|
|Luminance(TYP)				280 cd/m2											|
|Backlight Type				White LED*4											|
|Operation Temperature			-30~80(℃)											|
|Storage Temperature			-30~80(℃)											|
|---------------------------------------------------------------------------------------------------------------------------------------|
|-----------------------|														|
|Touch Screen Parameters|														|
|-----------------------|														|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Item					Parameters											|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Effective area size			2.8 inch											|
|Touch screen type			Capacitive touch screen										|
|Valid Area				240x320(pixels)											|
|Drive IC				D-FT6336G											|
|Visual Area				45.20(W)x59.45(H)(mm)										|
|interface				IIC												|
|Operation Temperature			-30~80(℃)											|
|Storage Temperature			-30~80(℃)											|
|---------------------------------------------------------------------------------------------------------------------------------------|
|---------------|															|
|Size Parameters|															|
|---------------|															|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Item					Parameters											|
|---------------------------------------------------------------------------------------------------------------------------------------|
|LCD screen size			50.00±0.2(W)x69.20±0.2(H)x2.3±0.1(D)								|
|Touch screen size			50.00±0.2(W)x69.20±0.2(H)x1.20 (D) ±0.1(D)							|
|Product size				Touch screen：50.00(W)x86.00(H)x10.60(D)								|
|					No Touch screen：50.00(W)x86.00(H)x9.10(D)							|
|---------------------------------------------------------------------------------------------------------------------------------------|
|---------------------------|														|
|Battery Charging Parameters|														|
|---------------------------|														|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Item					Parameters											|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Charging voltage			Range: 4.2~6.5(V)										|
|					Typical value: 5V										|
|																	|
|Charging current			Maximum: 500mA											|
|					Actual value of module: 290mA									|
|																	|
|Charging saturation voltage		4.24V												|
|Charging temperature			62℃												|
|Charging battery specifications	3.7V polymer lithium battery									|
|---------------------------------------------------------------------------------------------------------------------------------------|
|---------------------|															|
|Electrical Parameters|															|
|---------------------|															|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Item					Parameters											|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Operation Temperature			5.0V												|
|Backlight current			79mA												|
|Backlight brightness(actual value)	touch screen：230cd/m2										|
|					Touchless screen：270cd/m2									|
|																	|
|Total current				ESP32-S3 reset: 0										|
|					Only display works: 140 Display, speaker, battery charging all work: 560			|
|																	|
|Power					0.7（Only the display works)									|
|					2.8（Display, speaker, battery charging all work)						|
|---------------------------------------------------------------------------------------------------------------------------------------|
|--------------------|															|
|Interface Parameters|															|
|--------------------|															|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Item					Parameters											|
|---------------------------------------------------------------------------------------------------------------------------------------|
|ESP32-S3				Main control of the display module, controlling on-board and external peripherals		|
|																	|
|MicroSD Interface			Insert a Micro SD card to expand the storage space, such as storing font library, pictures,	|
|					audio files and other large data content.							|
|																	|
|RGB					LED lights with red, green, and blue colors, controlled by a single IO port.			|
|																	|
|UART					1.25mm 4P socket. Can be used for serial port debugging, downloading and communication. An	|
|					external USB to serial port module is required							|
|																	|
|Battery interface			1.25mm 2P socket. Used to connect to 3.7V polymer lithium battery, charge the battery through	|
|					the battery charging management circuit, also used for battery power |supply. Note the positive	|
|					and negative terminals of the interface								|
|																	|
|BOOT key				For entering the download mode or key test. Press and hold this key to power up,		|
|					then release to enter the download mode, or after powering up, and hold this key, then press	|
|					the RESET key, release the RESET key and then release this key, you can also enter the download	|
|					mode. When it is not necessary enter the download mode, this key can be used as a normal key.	|
|																	|
|TYPE-C interface			For module power and program downloading. This interface is connected to the one-key download	|
|					circuit on the module, which can realize automatic entry into the |download mode (without the	|
|					BOOT key).											|
|																	|
|RESET key				For ESP32S3 main control and LCD reset, press to reset the level.				|
|																	|
|Expand pin				1.25mm 4P socket. Pins GPIO2/3/14/21 are brought out						|
|																	|
|Horn interface				1.25mm 2P socket. For connecting to speaker to play audio. For connecting to speaker to play	|
|					audio (max 1.5W(Ω) or 2W (4Ω) speaker).								|
|																	|
|IIC interface				1.25mm 4P socket. For external SPI communication device, this SPI interface and MicroSD share.	|
|					Can be used as ordinary IO1.25mm 4P socket. For external IIC communication |device, this IIC	|
|					interface and capacitive touch screen share. Can be used as ordinary.				|
|---------------------------------------------------------------------------------------------------------------------------------------|
|--------------------|															|
|ESP32 Pin Parameters|															|
|--------------------|															|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Device					Pin					Pin Parameters						|
|---------------------------------------------------------------------------------------------------------------------------------------|
|LCD					IO10					LCD screen chip select control signal, low level active	|
|					IO46					LCD command/data select control signal;			|
|										High level: data; low level: command.			|
|																	|
|					IO12					LCD SPI bus clock signal				|
|					IO11					LCD SPI bus write data signal				|
|					IO13					LCD SPI bus read data signal				|
|					RST					LCD reset control signal, low level reset (share reset	|
|										pin with ESP32-S3 main control)				|
|					IO45					LCD backlight control signal (high level to turn on	|
|										backlight, low level to turn off backlight)		|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Touch Screen				IO16					Capacitive touch screen I2C bus data signal		|
|					IO15					Capacitive touch screen I2C bus clock signal		|
|					IO18					Capacitive touch screen reset control signal, low level	|
|										reset							|
|					IO17					Capacitive touch screen interrupt input signal, low	|
|										level input when a touch event occurs.			|
|---------------------------------------------------------------------------------------------------------------------------------------|
|RGB					IO42					Single-line RGB three-color LED light, which can	|
|										control the internal red, green, and blue three kinds	|
|										of light beads separately according to different signals|
|---------------------------------------------------------------------------------------------------------------------------------------|
|MicroSD				IO38					SD card SDIO bus clock signal				|
|					IO40					SD card SDIO bus command signal				|
|					IO39					SD card SDIO bus data signals (DATA0~DATA3 four data	|
|					IO41					lines (39/41/47/48)					|
|					IO48												|
|					IO47												|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Audio					IO1					Audio output enable signal, low level enable, high	|
|										level disable.						|
|					IO4					Audio I2S bus master clock signal			|
|					IO5					Audio I2S bus bit clock signal				|
|					IO6					Audio I2S bus bit output data signal			|
|					IO7					Audio I2S bus left and right channel selection signal.	|
|										High level: right channel; low level: left channel.	|
|					IO8					Audio I2S bus bit input data signal			|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Key					IO0					Download mode selection key (hold this key to power on,	|
|										then release to enter download mode)			|
|					EN					ESP32-s3 reset button, low level reset (share with LCD	|
|										reset)							|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Uart					RXD0(IO43)				ESP32-S3 UART0 RX signal				|
|					TXD0(IO44)				ESP32-S3 UART0 TX signal				|
|---------------------------------------------------------------------------------------------------------------------------------------|
|BATTERY				IO9					Battery voltage ADC value acquisition input signal	|
|---------------------------------------------------------------------------------------------------------------------------------------|
|Expand pin				IO2					Can be used as a normal IO (ADS)			|
|					IO3					Can be used as a normal IO (ADS)			|
|					IO14					Can be used as a normal IO (ADS)			|
|					IO21					Can be used as a normal IO (Digital Only)		|
|---------------------------------------------------------------------------------------------------------------------------------------|
|IIC interface				IO16					I2C bus data signal					|
|					IO15					I2C bus clock signal					|
|---------------------------------------------------------------------------------------------------------------------------------------|
