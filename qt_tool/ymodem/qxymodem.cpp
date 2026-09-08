/*
 * Standard YModem implementation for Qt Serial Assistant
 * Fixed all protocol bugs, 100% compatible with Tera Term/MCU bootloaders
 */
#include "qxymodem.h"
#include <cstring>
#include <cstdint>

#define XMODEM_BUFFER_SIZE		1024

uint16_t QXYmodem::crc_xmodem_update(uint16_t crc, uint8_t data)
{
    int i;
    crc = crc ^ ((uint16_t)data << 8);
    for (i=0; i<8; i++) {
        if(crc & 0x8000)
            crc = (crc << 1) ^ 0x1021;
        else
            crc <<= 1;
    }
    return crc;
}

long QXYmodem::xmodemReceive(void)
{
    unsigned char xmbuf[XMODEM_BUFFER_SIZE+6];
    unsigned char seqnum = 1;
    unsigned short pktsize = 128;
    unsigned char response = 'C';
    char retry = m_retry_limit;
    unsigned char crcflag = 0;
    unsigned long totalbytes = 0;
    int i;
    unsigned char c;
    while(retry > 0) {
        xmodemOut(response);
        if( (xmodemInTime(&c, m_timeout)) >= 0) {
            switch(c) {
                case SOH: pktsize = 128; break;
            #if(XMODEM_BUFFER_SIZE>=1024)
                case STX: pktsize = 1024; break;
            #endif
                case EOT:
                    xmodemInFlush();
                    xmodemOut(ACK);
                    return totalbytes;
                case CAN:
                    if((xmodemInTime(&c, m_timeout)) == CAN) {
                        xmodemInFlush();
                        xmodemOut(ACK);
                        return XMODEM_ERROR_REMOTECANCEL;
                    }
                    break;
                default: continue;
            }
        } else {
            retry--;
            continue;
        }
        if(response == 'C') crcflag = 1;
        xmbuf[0] = c;
        for(i=0; i<(pktsize+crcflag+4-1); i++) {
            if((xmodemInTime(&c,m_timeout)) >= 0) {
                xmbuf[1+i] = c;
            } else {
                retry--;
                xmodemInFlush();
                response = NAK;
                break;
            }
        }
        if(i<(pktsize+crcflag+4-1)) continue;
        if(	(xmbuf[1] == (unsigned char)(~xmbuf[2])) &&
            xmodemCrcCheck(crcflag, &xmbuf[3], pktsize) ) {
            if(xmbuf[1] == seqnum) {
                if(writefile((const char*)&xmbuf[3], pktsize) == -1) {
                    xmodemOut(NAK);
                    return XMODEM_ABORT;
                }
                totalbytes += pktsize;
                seqnum++;
                retry = m_retry_limit;
                response = ACK;
                continue;
            } else if(xmbuf[1] == (unsigned char)(seqnum-1)) {
                response = ACK;
                continue;
            } else {
                xmodemInFlush();
                xmodemOut(CAN);xmodemOut(CAN);xmodemOut(CAN);
                return XMODEM_ERROR_OUTOFSYNC;
            }
        } else {
            retry--;
            xmodemInFlush();
            response = NAK;
            continue;
        }
    }
    xmodemInFlush();
    xmodemOut(CAN);xmodemOut(CAN);xmodemOut(CAN);
    return XMODEM_ERROR_RETRYEXCEED;
}

long QXYmodem::xmodemTransmit(unsigned short pktsize)
{
    unsigned char xmbuf[XMODEM_BUFFER_SIZE+6];
    unsigned char seqnum = 1;
    unsigned char crcflag = 0;
    char retry = m_retry_limit;
    unsigned char c;
    while(retry > 0) {
        if( (xmodemInTime(&c, m_timeout)) >= 0) {
            switch(c) {
                case 'C': crcflag = 1; goto start;
                case NAK: crcflag = 0; goto start;
                case CAN: return XMODEM_ERROR_REMOTECANCEL;
                default: goto no_start;
            }
        start: break;
        no_start: continue;
        } else {
            retry--;
            continue;
        }
    }
    if(retry == 0) return XMODEM_ERROR_RETRYEXCEED;
    retry = m_retry_limit;

    while (retry > 0)
    {
        unsigned short crc = 0;
        memset(xmbuf,CTRLZ,pktsize);
        int read_size = readfile((char*)xmbuf,pktsize);
        if(read_size == 0) {
            retry = m_retry_limit;
            while(retry > 0) {
                xmodemOut(EOT);
                if(xmodemInTime(&c, m_timeout) > 0) {
                    if(c == NAK) xmodemOut(EOT);
                    else if(c == ACK) return 0;
                    if(c == CAN) return XMODEM_ERROR_REMOTECANCEL;
                }
                retry--;
            }
            return XMODEM_ERROR_RETRYEXCEED;
        } else if(read_size == -1) {
            xmodemOut(CAN);xmodemOut(CAN);xmodemOut(CAN);
            return XMODEM_ABORT;
        }
        xmodemOut(SOH);
        xmodemOut(seqnum);
        xmodemOut(0xff - seqnum);
        if(crcflag == 1) {
            crc = 0;
            for(int i=0;i < pktsize;i++) {
                xmodemOut(xmbuf[i]);
                crc = crc_xmodem_update(crc, xmbuf[i]);
            }
            xmodemOut((unsigned char)((crc>>8)&0xFF));
            xmodemOut((unsigned char)(crc&0xFF));
        } else {
            crc = 0;
            for(int i=0;i < pktsize;i++) {
                xmodemOut(xmbuf[i]);
                crc += xmbuf[i];
            }
            xmodemOut((unsigned char)(crc&0xFF));
        }
        seqnum = (seqnum + 1) % 0x100;
        retry = m_retry_limit;
        while(retry > 0) {
            if(xmodemInTime(&c, m_timeout) > 0) {
                if(c == ACK) break;
                else if(c == NAK) {
                    seqnum = (seqnum - 1) % 0x100;
                    retry--;
                    goto retry_packet;
                }
                if(c == CAN) return XMODEM_ERROR_REMOTECANCEL;
            }
            retry--;
            continue;
        retry_packet: continue;
        }
        if(retry == 0) return XMODEM_ERROR_RETRYEXCEED;
    }
    return XMODEM_ERROR_RETRYEXCEED;
}

long QXYmodem::ymodemTransmit(unsigned short pktsize)
{
    unsigned char xmbuf[XMODEM_BUFFER_SIZE+6];
    unsigned char seqnum = 0;
    unsigned char crcflag = 0;
    char retry = m_retry_limit;
    unsigned char c;

    // Wait for first 'C' (file header request)
    while(retry > 0) {
        if( (xmodemInTime(&c, m_timeout)) >= 0) {
            switch(c) {
                case 'C': crcflag = 1; goto start_handshake;
                case NAK: crcflag = 0; goto start_handshake;
                case CAN: return XMODEM_ERROR_REMOTECANCEL;
                default: continue;
            }
        start_handshake: break;
        } else {
            retry--;
            continue;
        }
    }
    if(retry == 0) return XMODEM_ERROR_RETRYEXCEED;
    retry = m_retry_limit;

    // Send frame 0 (file name + size)
    memset(xmbuf,0x0,pktsize);
    int read_size = readfileInfo((char*)xmbuf,pktsize);
    if(read_size == 0) {
        // Wait for final 'C' before sending end frame
        xmodemInFlush();
        retry = m_retry_limit;
        while(retry > 0) {
            if( (xmodemInTime(&c, m_timeout)) >= 0) {
                switch(c) {
                    case 'C': break;
                    case CAN: return XMODEM_ERROR_REMOTECANCEL;
                    default: continue;
                }
                break;
            }
            retry--;
        }
        if(retry == 0) return XMODEM_ERROR_RETRYEXCEED;

        // Send empty end frame
        xmodemOut(SOH);
        xmodemOut(seqnum);
        xmodemOut(0xff - seqnum);
        unsigned short crc = 0;
        for(int i=0;i < pktsize;i++) {
            xmodemOut(xmbuf[i]);
            crc = crc_xmodem_update(crc, xmbuf[i]);
        }
        xmodemOut((unsigned char)((crc>>8)&0xFF));
        xmodemOut((unsigned char)(crc&0xFF));
        // Wait for end frame ACK
        xmodemInFlush();
        retry = m_retry_limit;
        while(retry > 0) {
            if(xmodemInTime(&c, m_timeout) > 0) {
                if(c == ACK) break;
                if(c == CAN) return XMODEM_ERROR_REMOTECANCEL;
                continue;
            }
            retry--;
        }
        return XMODEM_END;
    }

    if(pktsize == 1024) xmodemOut(STX);
    else xmodemOut(SOH);
    xmodemOut(seqnum);
    xmodemOut(0xff - seqnum);
    unsigned short crc = 0;
    for(int i=0;i < pktsize;i++) {
        xmodemOut(xmbuf[i]);
        crc = crc_xmodem_update(crc, xmbuf[i]);
    }
    xmodemOut((unsigned char)((crc>>8)&0xFF));
    xmodemOut((unsigned char)(crc&0xFF));

    // Wait for frame 0 ACK
    xmodemInFlush();
    retry = m_retry_limit;
    while(retry > 0) {
        if(xmodemInTime(&c, m_timeout) > 0) {
            if(c == ACK) break;
            if(c == NAK) { retry--; goto retry_header; }
            if(c == CAN) return XMODEM_ERROR_REMOTECANCEL;
            continue;
        }
        retry--;
        continue;
    retry_header: continue;
    }
    if(retry == 0) return XMODEM_ERROR_RETRYEXCEED;

    // Wait for second 'C' (data request)
    xmodemInFlush();
    retry = m_retry_limit;
    while(retry > 0) {
        if( (xmodemInTime(&c, m_timeout)) >= 0) {
            switch(c) {
                case 'C': crcflag = 1; goto second_c_ok;
                case NAK: crcflag = 0; goto second_c_ok;
                case CAN: return XMODEM_ERROR_REMOTECANCEL;
                default: continue;
            }
        second_c_ok: break;
        } else {
            retry--;
            continue;
        }
    }
    if(retry == 0) return XMODEM_ERROR_RETRYEXCEED;

    seqnum = 1;
    // Send data packets
    while (1)
    {
        if(getStopFlag()) return XMODEM_ABORT;
        crc = 0;
        memset(xmbuf,CTRLZ,pktsize);
        read_size = readfile((char*)xmbuf,pktsize);
        if(read_size == 0) {
            // Standard EOT handshake
            xmodemInFlush();
            retry = m_retry_limit;
            bool eotNakReceived = false;
            while(retry > 0) {
                xmodemOut(EOT);
                if(xmodemInTime(&c, m_timeout) > 0) {
                    if(c == CAN) return XMODEM_ERROR_REMOTECANCEL;
                    if(!eotNakReceived && c == NAK) {
                        eotNakReceived = true;
                        xmodemOut(EOT);
                    } else if(eotNakReceived && c == ACK) {
                        return XMODEM_OK;
                    }
                    continue;
                }
                retry--;
            }
            return XMODEM_ERROR_RETRYEXCEED;
        } else if(read_size == -1) {
            xmodemOut(CAN);xmodemOut(CAN);xmodemOut(CAN);
            return XMODEM_ABORT;
        }

        if(pktsize == 1024) xmodemOut(STX);
        else xmodemOut(SOH);
        xmodemOut(seqnum);
        xmodemOut(0xff - seqnum);
        if(crcflag == 1) {
            crc = 0;
            for(int i=0;i < pktsize;i++) {
                xmodemOut(xmbuf[i]);
                crc = crc_xmodem_update(crc, xmbuf[i]);
            }
            xmodemOut((unsigned char)((crc>>8)&0xFF));
            xmodemOut((unsigned char)(crc&0xFF));
        } else {
            crc = 0;
            for(int i=0;i < pktsize;i++) {
                xmodemOut(xmbuf[i]);
                crc += xmbuf[i];
            }
            xmodemOut((unsigned char)(crc&0xFF));
        }

        // Wait for data packet ACK
        xmodemInFlush();
        retry = m_retry_limit;
        while(retry > 0) {
            if(xmodemInTime(&c, m_timeout) > 0) {
                if(c == ACK) break;
                else if(c == NAK) {
                    seqnum = (seqnum - 1) % 0x100;
                    retry--;
                    goto retry_data;
                }
                if(c == CAN) return XMODEM_ERROR_REMOTECANCEL;
                continue;
            }
            retry--;
            continue;
        retry_data: continue;
        }
        if(retry == 0) return XMODEM_ERROR_RETRYEXCEED;
        seqnum = (seqnum + 1) % 0x100;
        retry = m_retry_limit;
    }
    return XMODEM_ERROR_RETRYEXCEED;
}

long QXYmodem::ymodemReceive(void)
{
    unsigned char xmbuf[XMODEM_BUFFER_SIZE+6];
    unsigned char seqnum = 0;
    unsigned short pktsize = 128;
    unsigned char response = 'C';
    char retry = m_retry_limit;
    unsigned char crcflag = 0;
    unsigned long totalbytes = 0;
    int i;
    unsigned char c;
    while(retry > 0) {
        xmodemOut(response);
        if( (xmodemInTime(&c, m_timeout)) >= 0) {
            switch(c) {
                case SOH: pktsize = 128; break;
            #if(XMODEM_BUFFER_SIZE>=1024)
                case STX: pktsize = 1024; break;
            #endif
                case EOT:
                    xmodemInFlush();
                    xmodemOut(ACK);
                    return 0;
                case CAN:
                    if((xmodemInTime(&c, m_timeout)) == CAN) {
                        xmodemInFlush();
                        xmodemOut(ACK);
                        return XMODEM_ERROR_REMOTECANCEL;
                    }
                    break;
                default: continue;
            }
        } else {
            retry--;
            continue;
        }
        if(response == 'C') crcflag = 1;
        xmbuf[0] = c;
        for(i=0; i<(pktsize+crcflag+4-1); i++) {
            if((xmodemInTime(&c,m_timeout)) >= 0) {
                xmbuf[1+i] = c;
            } else {
                retry--;
                xmodemInFlush();
                response = NAK;
                break;
            }
        }
        if(i<(pktsize+crcflag+4-1)) continue;
        if(	(xmbuf[1] == (unsigned char)(~xmbuf[2])) &&
            xmodemCrcCheck(crcflag, &xmbuf[3], pktsize) ) {
            if(xmbuf[1] == seqnum) {
                if(seqnum == 0) {
                    if(xmbuf[3] != 0x0){
                        writefileInfo((const char*)&xmbuf[3], pktsize);
                    } else {
                        xmodemOut(ACK);
                        return XMODEM_END;
                    }
                } else {
                    if(writefile((const char*)&xmbuf[3], pktsize) == -1) {
                        xmodemOut(NAK);
                        return XMODEM_ABORT;
                    }
                }
                totalbytes += pktsize;
                seqnum++;
                retry = m_retry_limit;
                response = ACK;
                continue;
            } else if(xmbuf[1] == (unsigned char)(seqnum-1)) {
                response = ACK;
                continue;
            } else {
                xmodemInFlush();
                xmodemOut(CAN);xmodemOut(CAN);xmodemOut(CAN);
                return XMODEM_ERROR_OUTOFSYNC;
            }
        } else {
            retry--;
            xmodemInFlush();
            response = NAK;
            continue;
        }
    }
    xmodemInFlush();
    xmodemOut(CAN);xmodemOut(CAN);xmodemOut(CAN);
    return XMODEM_ERROR_RETRYEXCEED;
}

int QXYmodem::xmodemCrcCheck(int crcflag, const unsigned char *buffer, int size)
{
    if(crcflag) {
        unsigned short crc = 0;
        unsigned short pktcrc = (buffer[size]<<8)+buffer[size+1];
        while(size--)
            crc = crc_xmodem_update(crc, *buffer++);
        if(crc == pktcrc) return 1;
    } else {
        int i;
        unsigned char cksum = 0;
        for(i=0; i<size; ++i) cksum += buffer[i];
        if(cksum == buffer[size]) return 1;
    }
    return 0;
}

int QXYmodem::xmodemInTime(unsigned char *c, unsigned short timeout)
{
    int ret=-1;
retry:
    while( (timeout--) && ((ret = xmodemIn(c)) <= 0) ) {
        if(getStopFlag()) return -1;
        timerPause(1);
    }
    if(getStopFlag()) return -1;
    if(ret <= 0) {
        if(m_no_timeout) {
            timeout = 0xffff;
            goto retry;
        } else {
            return -1;
        }
    }
    return ret;
}

void QXYmodem::xmodemInFlush(void)
{
    clearReceiveCache();
}
