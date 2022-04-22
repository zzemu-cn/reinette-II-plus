#ifndef APPLE2LOG_H_
#define APPLE2LOG_H_

void showDiskMotor(uint16_t address, int q)
{
	address &= 7;
	LOG("Motor%d %d PC %04X: %d %d %d%d%d%d %d\n",curDrv, disk[curDrv].motorOn, getPC(), address>>1, address&1,
		phs[curDrv][0], phs[curDrv][1], phs[curDrv][2], phs[curDrv][3], q);
}

void showDiskMark()
{
	LOG("W ADR_H %02X %02X %02X  ", readMem(0xBC7A), readMem(0xBC7F), readMem(0xBC84));
	LOG("W DAT_H %02X %02X %02X  ", readMem(0xB853), readMem(0xB858), readMem(0xB85D));
	LOG("W ADR_E %02X %02X %02X  ", readMem(0xBCAE), readMem(0xBCB3), readMem(0xBCB8));
	LOG("W DAT_E %02X %02X %02X\n", readMem(0xB89E), readMem(0xB8A3), readMem(0xB8A8));

	LOG("R ADR_H %02X %02X %02X  ", readMem(0xB955), readMem(0xB95F), readMem(0xB96A));
	LOG("R DAT_H %02X %02X %02X  ", readMem(0xB8E7), readMem(0xB8F1), readMem(0xB8FC));
	LOG("R ADR_E %02X %02X %02X  ", readMem(0xB991), readMem(0xB99B), 0xEB);
	LOG("R DAT_E %02X %02X %02X\n", readMem(0xB935), readMem(0xB93F), 0xEB);
}


void showC65C()
{
	LOG("C65C T %d S %d >> %02X%02X\n", readMem(0x0041), readMem(0x003D), readMem(0x0027), readMem(0x0026));
}

void showRWTS()
{
	uint16_t IOB;

	IOB = getA();
	IOB = IOB<<8;
	IOB = IOB|getY();
	LOG("RWTS SP:%04X(%02X%02X) IOB %04X CMD %d T %d S %d >> %02X%02X\n", getSP(), readMem(0x100+getSP()+2), readMem(0x100+getSP()+1), IOB, readMem(IOB+12), readMem(IOB+4), readMem(IOB+5), readMem(IOB+9), readMem(IOB+8));
}

#endif	// APPLE2LOG_H_
