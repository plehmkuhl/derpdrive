#include "cartridge.h"

#include <QFile>
#include <QDebug>
#include <QtEndian>

#pragma pack(push, 1)
struct CartridgeHeader {
    quint32  vectors           [0x40];
    char     consoleName       [0x10];
    char     copyright         [0x10];
    char     domesticGameName  [0x30];
    char     overseasGameName  [0x30];
    char     type              [0x03];
    char     productCode       [0x0B];
    quint16  checksum;
    char     ioSupport         [0x10];
    quint32  romStart;
    quint32  romEnd;
    quint32  ramStart;
    quint32  ramEnd;
    char     sramFlag          [0x02];
    char     reserved;
    quint32  sramStart;
    quint32  sramEnd;
    char     modem             [0x0C];
    char     memo              [0x28];
    char     country           [0x0F];
};
#pragma pack(pop)

class CartridgePrivate {
public:
    CartridgeHeader*  header;
    QByteArray        romData;
    QByteArray        ramData;
    QByteArray        sramData;
    bool              ssfiiBankswitch;
    bool              ssfiiBankswitchEnabled;
    QVector<quint8>   selectedBank;

public:
    CartridgePrivate(Cartridge* q)
        : q_ptr(q),
          header(0),
          ssfiiBankswitch(false),
          ssfiiBankswitchEnabled(false)
    {
        qDebug() << "Header size" << sizeof(CartridgeHeader);

        this->selectedBank.resize(7);
    }

    ~CartridgePrivate() {
        if (this->header)
            delete this->header;
    }

    quint16 calculateChecksum(QByteArray rom) {
        quint16 sum = 0;
        //quint16* word = (quint16*)rom.data();

        for(int i=sizeof(CartridgeHeader); i < rom.length(); i++) {
            sum += rom[i];
        }

        return sum;
    }

    bool readHeader(QFile* file, CartridgeHeader* header) {
        if (file->read((char*)header, sizeof(CartridgeHeader)) != sizeof(CartridgeHeader))
            return false;

        header->checksum  = qFromBigEndian(header->checksum);
        header->romStart  = qFromBigEndian(header->romStart);
        header->romEnd    = qFromBigEndian(header->romEnd);
        header->ramStart  = qFromBigEndian(header->ramStart);
        header->ramEnd    = qFromBigEndian(header->ramEnd);
        header->sramStart = qFromBigEndian(header->sramStart);
        header->sramEnd   = qFromBigEndian(header->sramEnd);

        qDebug() << "Console:"     << QString::fromLatin1(header->consoleName, sizeof(CartridgeHeader::consoleName));
        qDebug() << "Copyright:"   << QString::fromLatin1(header->copyright, sizeof(CartridgeHeader::copyright));
        qDebug() << "Name (US):"   << QString::fromLatin1(header->domesticGameName, sizeof(CartridgeHeader::domesticGameName));
        qDebug() << "Name (EU):"   << QString::fromLatin1(header->overseasGameName, sizeof(CartridgeHeader::overseasGameName));
        qDebug() << "Type:"        << QString::fromLatin1(header->type, sizeof(CartridgeHeader::type));
        qDebug() << "Product:"     << QString::fromLatin1(header->productCode, sizeof(CartridgeHeader::productCode));
        qDebug() << "Checksum:"    << header->checksum;
        qDebug() << "IO:"          << QString::fromLatin1(header->ioSupport, sizeof(CartridgeHeader::ioSupport));
        qDebug() << "Rom Start:"   << header->romStart;
        qDebug() << "Rom End:"     << header->romEnd;
        qDebug() << "Ram Start:"   << header->ramStart;
        qDebug() << "Ram End:"     << header->ramEnd;
        qDebug() << "Ram Flags:"   << QString::fromLatin1(header->sramFlag, sizeof(CartridgeHeader::sramFlag));
        qDebug() << "SRam Start:"  << header->sramStart;
        qDebug() << "SRam End:"    << header->sramEnd;
        qDebug() << "Modem:"       << QString::fromLatin1(header->modem, sizeof(CartridgeHeader::modem));
        qDebug() << "Memo:"        << QString::fromLatin1(header->memo, sizeof(CartridgeHeader::memo));
        qDebug() << "Country:"     << QString::fromLatin1(header->country, sizeof(CartridgeHeader::country));

        return true;
    }

    QByteArray decodeInterleaved(QByteArray data) {
        QByteArray decoded(data.length(), 0);

        int midpoint = data.length() / 2;
        int o=1, e=0;

        for(int i=0; i < midpoint; i++) {
            decoded[o] = data[i];
            decoded[e] = data[i + midpoint];
            o += 2;
            e += 2;
        }

        return decoded;
    }

    void readBIN(QString path) {
        QScopedPointer<CartridgeHeader> header(new CartridgeHeader());

        QByteArray romData;

        QFile rom(path);
        if(!rom.open(QFile::ReadOnly)) {
            qCritical() << "Failed to lead rom" << path;
            return;
        }

        if(!this->readHeader(&rom, header.data())) {
            qCritical() << "Invalid ROM header!";
            return;
        }

        int len = header->romEnd - header->romStart;
        int middlepoint = len / 2;

        if (len > rom.size()) {
            qCritical() << "Invalid ROM header!";
        }

        // Re-Read header for rom storage
        rom.seek(0);

        // Read rom data
        romData.append(rom.readAll());
        if(romData.length() < len)
            romData.append(QByteArray(romData.length() - len, 0));

        quint16 checksum = this->calculateChecksum(romData);
        if (checksum != header->checksum) {
            qCritical() << "Checksum mismatch!";
            qCritical() << "Expected:" << header->checksum;
            qCritical() << "Got:" << checksum;
            //return; So what?
        }

        this->readROM(header.take(), romData);
    }

    // Reads the MD Format according to
    // https://www.zophar.net/fileuploads/2/10614uauyw/Genesis_ROM_Format.txt
    void readMD(QString path) {
        QScopedPointer<CartridgeHeader> header(new CartridgeHeader());

        QByteArray romData;

        QFile rom(path);
        if(!rom.open(QFile::ReadOnly)) {
            qCritical() << "Failed to lead rom" << path;
            return;
        }

        if(!this->readHeader(&rom, header.data())) {
            qCritical() << "Invalid ROM header!";
            return;
        }

        int len = header->romEnd - header->romStart;
        int middlepoint = len / 2;

        if (len > rom.size()) {
            qCritical() << "Invalid ROM header!";
            return;
        }

        // Re-Read header for rom storage
        rom.seek(0);
        romData.append(rom.read(sizeof(CartridgeHeader)));

        // Read rom data
        romData.append(rom.readAll()); //this->decodeInterleaved(rom.readAll()));
        // Decode MD format
        /*int headerOffset = sizeof(CartridgeHeader);
         int o = 1, e = 0;
         for(int i=0; i < len; i++) {
            char byte;
            rom.read(&byte, 1);

            //romData[i] = byte;
            if (i <= middlepoint) {
               romData[headerOffset + (i*2)] = byte;
            } else {
               romData[headerOffset + (i*2 - len - 1)] = byte;
            }
         }*/

        quint16 checksum = this->calculateChecksum(romData);
        if (checksum != header->checksum) {
            qCritical() << "Checksum mismatch!";
            qCritical() << "Expected:" << header->checksum;
            qCritical() << "Got:" << checksum;
            //return; So what?
        }

        this->readROM(header.take(), romData);
    }

    void readSMD(QString path) {

    }

    void readROM(CartridgeHeader* header, QByteArray data) {
        this->header = header;
        this->romData = data;

        quint32 ramSize = header->ramEnd - header->ramStart;
        qDebug() << "Ram size:" << ramSize;

        // Check for legal ram size
        if(ramSize > 1024 * 1024) {
            qCritical() << "RAM is too big!";
            return;
        }

        this->ramData.resize(ramSize);

        // SRAM Enabled?
        if (QString::fromLatin1(header->sramFlag, sizeof(CartridgeHeader::sramFlag)) == "RA" || (unsigned char)header->sramFlag[0] == 0xF8) {
            quint32 sramSize = header->sramEnd - header->sramStart;
            qDebug() << "SRam size:" << ramSize;

            // Check for legal sram size
            if(ramSize > 1024 * 1024) {
                qCritical() << "SRAM is too big!";
                return;
            }

            this->sramData.resize(ramSize);
        } else {
            this->sramData.clear();
        }

        // Check for ssfii style bank switching
        if (data.length() > 0x400000) {
            this->ssfiiBankswitch = true;
            qDebug() << "Emulating SSF2 Bankswitch";
        }
    }

    void unload() {
        free(this->header);
        this->header = 0;

        this->ssfiiBankswitch = false;
        this->ssfiiBankswitchEnabled = false;
        this->selectedBank.fill(0);

        this->romData.clear();
        this->ramData.clear();
    }

public:
    Cartridge* q_ptr;
    Q_DECLARE_PUBLIC(Cartridge)
};

Cartridge::Cartridge(QObject *parent)
    : QObject(parent),
      d_ptr(new CartridgePrivate(this))
{

}

Cartridge::~Cartridge()
{
    delete d_ptr;
}

void Cartridge::load(QString path)
{
    Q_D(Cartridge);

    d->unload();

    // Detect rom format
    if (path.right(4).toLower() == ".bin" || path.right(4).toLower() == ".md" || path.right(4).toLower() == ".gen") {
        d->readBIN(path);
    } else if (path.right(3).toLower() == ".md") {
        d->readMD(path);
    } else if(path.right(4).toLower() == ".smd") {
        d->readSMD(path);
    } else {
        qCritical() << "Unknown file format";
        return;
    }
}

int Cartridge::peek(quint32 address, quint8& val) {
    Q_D(Cartridge);

    // ROM access
    if (address >= d->header->romStart && address <= d->header->romEnd && (!d->ssfiiBankswitch || !d->ssfiiBankswitchEnabled)) {
        val = d->romData[address] - d->header->romStart;
        return NO_ERROR;

        // SSFII Fixed Rom
    } else if (d->ssfiiBankswitch && address >= 0 && address <= 0x07FFFF) {
        val = d->romData[address];
        return NO_ERROR;

        // SSFII Bank 0
    } else if (d->ssfiiBankswitch && address >= 0x080000 && address <= 0x0FFFFF) {
        if (d->selectedBank[0])
            val = d->romData[d->selectedBank[0] * 0x80000 + address - 0x080000];
        else
            val = d->romData[address];
        return NO_ERROR;

        // SSFII Bank 1
    } else if (d->ssfiiBankswitch && address >= 0x100000 && address <= 0x17FFFF) {
        if (d->selectedBank[1])
            val = d->romData[d->selectedBank[1] * 0x80000 + address - 0x100000];
        else
            val = d->romData[address];
        return NO_ERROR;

        // SSFII Bank 2
    } else if (d->ssfiiBankswitch && address >= 0x180000 && address <= 0x1FFFFF) {
        if (d->selectedBank[2])
            val = d->romData[d->selectedBank[2] * 0x80000 + address - 0x180000];
        else
            val = d->romData[address];
        return NO_ERROR;

        // SSFII Bank 3
    } else if (d->ssfiiBankswitch && address >= 0x200000 && address <= 0x27FFFF) {
        if (d->selectedBank[3])
            val = d->romData[d->selectedBank[3] * 0x80000 + address - 0x200000];
        else
            val = d->romData[address];
        return NO_ERROR;

        // SSFII Bank 4
    } else if (d->ssfiiBankswitch && address >= 0x280000 && address <= 0x2FFFFF) {
        if (d->selectedBank[4])
            val = d->romData[d->selectedBank[4] * 0x80000 + address - 0x280000];
        else
            val = d->romData[address];
        return NO_ERROR;

        // SSFII Bank 5
    } else if (d->ssfiiBankswitch && address >= 0x300000 && address <= 0x37FFFF) {
        if (d->selectedBank[5])
            val = d->romData[d->selectedBank[5] * 0x80000 + address - 0x300000];
        else
            val = d->romData[address];
        return NO_ERROR;

        // SSFII Bank 6
    } else if (d->ssfiiBankswitch && address >= 0x380000 && address <= 0x3FFFFF) {
        if (d->selectedBank[6])
            val = d->romData[d->selectedBank[6] * 0x80000 + address - 0x380000];
        else
            val = d->romData[address];
        return NO_ERROR;

        // RAM access
    } else if (address >= d->header->ramStart && address <= d->header->ramEnd) {
        val = d->ramData[address - d->header->ramStart];
        return NO_ERROR;

        // SRAM access
    } else if (address >= d->header->sramStart && address <= d->header->sramEnd) {
        val = d->sramData[address - d->header->sramStart];
        return NO_ERROR;
    }

    // What are you even trying?
    return BUS_ERROR;
}

int Cartridge::poke(quint32 address, quint8 val) {
    Q_D(Cartridge);

    //We only allow writing to RAM and SRAM, everything else is a noop

    // RAM access
    if (address >= d->header->ramStart && address <= d->header->ramEnd-2) {
        d->ramData[address - d->header->ramStart] = val;

        // SRAM access
    } else if (address >= d->header->sramStart && address <= d->header->sramEnd-2) {
        d->sramData[address - d->header->sramStart] = val;

        // SSFII Bankswitching
    } else if (address == 0xA130F1) {
        d->ssfiiBankswitchEnabled = true;
    } else if (address >= 0xA130F3 && address <= 0xA131FF && d->ssfiiBankswitch) {
        d->ssfiiBankswitchEnabled = true;

        qDebug() << "Bank switch" << QString::number(address,16).rightJustified(6, '0') << "(" << (((address & 0xF) >> 1) - 1) << ")" << val;
        d->selectedBank[((address & 0xF) >> 1) - 1] = val;
    }

    return NO_ERROR;
}
