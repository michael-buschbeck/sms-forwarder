// SMSForwarder
// 2025 michael@buschbeck.net

// Arduino Uno R3 (AVR)
// a-gsmII shield by itbrainpower.net

// Increase the serial RX buffer to ensure reliable communcation with the GSM modem:
// %LocalAppData%\Arduino15\packages\arduino\hardware\avr\1.8.6\boards.txt
// uno.build.extra_flags=-D_SS_MAX_RX_BUFF=128

#include <Arduino.h>
#include <SoftwareSerial.h>

#define MODEM_SERIAL_RX_PIN 2
#define MODEM_SERIAL_TX_PIN 3
#define MODEM_STATUS_PIN    5
#define MODEM_RESET_PIN     6
#define MODEM_POWER_PIN     7

#define SIM_PIN "0000"

enum struct ModemResult : uint8_t
{
    OK          = 0,
    Connect     = 1,
    Ring        = 2,
    NoCarrier   = 3,
    Error       = 4,
    NoDialtone  = 6,
    Busy        = 7,
    NoAnswer    = 8,
    Proceeding  = 9,
    None        = 0xFF,
};

enum struct Encoding : uint8_t
{
    UTF8     = 0,
    GSMBYTES = 1,
    UCS2LE   = 2,
    UCS2BE   = 3,
    Unknown  = 0xFF,
};

const wchar_t GSMToUnicode[] PROGMEM =
{
    /*GSM 0x00 = Unicode*/ 0x0040,
    /*GSM 0x01 = Unicode*/ 0x00A3,
    /*GSM 0x02 = Unicode*/ 0x0024,
    /*GSM 0x03 = Unicode*/ 0x00A5,
    /*GSM 0x04 = Unicode*/ 0x00E8,
    /*GSM 0x05 = Unicode*/ 0x00E9,
    /*GSM 0x06 = Unicode*/ 0x00F9,
    /*GSM 0x07 = Unicode*/ 0x00EC,
    /*GSM 0x08 = Unicode*/ 0x00F2,
    /*GSM 0x09 = Unicode*/ 0x00E7,
    /*GSM 0x0A = Unicode*/ 0x000A,
    /*GSM 0x0B = Unicode*/ 0x00D8,
    /*GSM 0x0C = Unicode*/ 0x00F8,
    /*GSM 0x0D = Unicode*/ 0x000D,
    /*GSM 0x0E = Unicode*/ 0x00C5,
    /*GSM 0x0F = Unicode*/ 0x00E5,
    /*GSM 0x10 = Unicode*/ 0x0394,
    /*GSM 0x11 = Unicode*/ 0x005F,
    /*GSM 0x12 = Unicode*/ 0x03A6,
    /*GSM 0x13 = Unicode*/ 0x0393,
    /*GSM 0x14 = Unicode*/ 0x039B,
    /*GSM 0x15 = Unicode*/ 0x03A9,
    /*GSM 0x16 = Unicode*/ 0x03A0,
    /*GSM 0x17 = Unicode*/ 0x03A8,
    /*GSM 0x18 = Unicode*/ 0x03A3,
    /*GSM 0x19 = Unicode*/ 0x0398,
    /*GSM 0x1A = Unicode*/ 0x039E,
    /*GSM 0x1B = Unicode*/ 0x00A0,
    /*GSM 0x1C = Unicode*/ 0x00C6,
    /*GSM 0x1D = Unicode*/ 0x00E6,
    /*GSM 0x1E = Unicode*/ 0x00DF,
    /*GSM 0x1F = Unicode*/ 0x00C9,
    /*GSM 0x20 = Unicode*/ 0x0020,
    /*GSM 0x21 = Unicode*/ 0x0021,
    /*GSM 0x22 = Unicode*/ 0x0022,
    /*GSM 0x23 = Unicode*/ 0x0023,
    /*GSM 0x24 = Unicode*/ 0x00A4,
    /*GSM 0x25 = Unicode*/ 0x0025,
    /*GSM 0x26 = Unicode*/ 0x0026,
    /*GSM 0x27 = Unicode*/ 0x0027,
    /*GSM 0x28 = Unicode*/ 0x0028,
    /*GSM 0x29 = Unicode*/ 0x0029,
    /*GSM 0x2A = Unicode*/ 0x002A,
    /*GSM 0x2B = Unicode*/ 0x002B,
    /*GSM 0x2C = Unicode*/ 0x002C,
    /*GSM 0x2D = Unicode*/ 0x002D,
    /*GSM 0x2E = Unicode*/ 0x002E,
    /*GSM 0x2F = Unicode*/ 0x002F,
    /*GSM 0x30 = Unicode*/ 0x0030,
    /*GSM 0x31 = Unicode*/ 0x0031,
    /*GSM 0x32 = Unicode*/ 0x0032,
    /*GSM 0x33 = Unicode*/ 0x0033,
    /*GSM 0x34 = Unicode*/ 0x0034,
    /*GSM 0x35 = Unicode*/ 0x0035,
    /*GSM 0x36 = Unicode*/ 0x0036,
    /*GSM 0x37 = Unicode*/ 0x0037,
    /*GSM 0x38 = Unicode*/ 0x0038,
    /*GSM 0x39 = Unicode*/ 0x0039,
    /*GSM 0x3A = Unicode*/ 0x003A,
    /*GSM 0x3B = Unicode*/ 0x003B,
    /*GSM 0x3C = Unicode*/ 0x003C,
    /*GSM 0x3D = Unicode*/ 0x003D,
    /*GSM 0x3E = Unicode*/ 0x003E,
    /*GSM 0x3F = Unicode*/ 0x003F,
    /*GSM 0x40 = Unicode*/ 0x00A1,
    /*GSM 0x41 = Unicode*/ 0x0041,
    /*GSM 0x42 = Unicode*/ 0x0042,
    /*GSM 0x43 = Unicode*/ 0x0043,
    /*GSM 0x44 = Unicode*/ 0x0044,
    /*GSM 0x45 = Unicode*/ 0x0045,
    /*GSM 0x46 = Unicode*/ 0x0046,
    /*GSM 0x47 = Unicode*/ 0x0047,
    /*GSM 0x48 = Unicode*/ 0x0048,
    /*GSM 0x49 = Unicode*/ 0x0049,
    /*GSM 0x4A = Unicode*/ 0x004A,
    /*GSM 0x4B = Unicode*/ 0x004B,
    /*GSM 0x4C = Unicode*/ 0x004C,
    /*GSM 0x4D = Unicode*/ 0x004D,
    /*GSM 0x4E = Unicode*/ 0x004E,
    /*GSM 0x4F = Unicode*/ 0x004F,
    /*GSM 0x50 = Unicode*/ 0x0050,
    /*GSM 0x51 = Unicode*/ 0x0051,
    /*GSM 0x52 = Unicode*/ 0x0052,
    /*GSM 0x53 = Unicode*/ 0x0053,
    /*GSM 0x54 = Unicode*/ 0x0054,
    /*GSM 0x55 = Unicode*/ 0x0055,
    /*GSM 0x56 = Unicode*/ 0x0056,
    /*GSM 0x57 = Unicode*/ 0x0057,
    /*GSM 0x58 = Unicode*/ 0x0058,
    /*GSM 0x59 = Unicode*/ 0x0059,
    /*GSM 0x5A = Unicode*/ 0x005A,
    /*GSM 0x5B = Unicode*/ 0x00C4,
    /*GSM 0x5C = Unicode*/ 0x00D6,
    /*GSM 0x5D = Unicode*/ 0x00D1,
    /*GSM 0x5E = Unicode*/ 0x00DC,
    /*GSM 0x5F = Unicode*/ 0x00A7,
    /*GSM 0x60 = Unicode*/ 0x00BF,
    /*GSM 0x61 = Unicode*/ 0x0061,
    /*GSM 0x62 = Unicode*/ 0x0062,
    /*GSM 0x63 = Unicode*/ 0x0063,
    /*GSM 0x64 = Unicode*/ 0x0064,
    /*GSM 0x65 = Unicode*/ 0x0065,
    /*GSM 0x66 = Unicode*/ 0x0066,
    /*GSM 0x67 = Unicode*/ 0x0067,
    /*GSM 0x68 = Unicode*/ 0x0068,
    /*GSM 0x69 = Unicode*/ 0x0069,
    /*GSM 0x6A = Unicode*/ 0x006A,
    /*GSM 0x6B = Unicode*/ 0x006B,
    /*GSM 0x6C = Unicode*/ 0x006C,
    /*GSM 0x6D = Unicode*/ 0x006D,
    /*GSM 0x6E = Unicode*/ 0x006E,
    /*GSM 0x6F = Unicode*/ 0x006F,
    /*GSM 0x70 = Unicode*/ 0x0070,
    /*GSM 0x71 = Unicode*/ 0x0071,
    /*GSM 0x72 = Unicode*/ 0x0072,
    /*GSM 0x73 = Unicode*/ 0x0073,
    /*GSM 0x74 = Unicode*/ 0x0074,
    /*GSM 0x75 = Unicode*/ 0x0075,
    /*GSM 0x76 = Unicode*/ 0x0076,
    /*GSM 0x77 = Unicode*/ 0x0077,
    /*GSM 0x78 = Unicode*/ 0x0078,
    /*GSM 0x79 = Unicode*/ 0x0079,
    /*GSM 0x7A = Unicode*/ 0x007A,
    /*GSM 0x7B = Unicode*/ 0x00E4,
    /*GSM 0x7C = Unicode*/ 0x00F6,
    /*GSM 0x7D = Unicode*/ 0x00F1,
    /*GSM 0x7E = Unicode*/ 0x00FC,
    /*GSM 0x7F = Unicode*/ 0x00E0,
};

template<typename from_t, typename to_t>
struct Mapping
{
    from_t from;
    to_t   to;
};

const Mapping<char, wchar_t> GSMToUnicodeExtension[] PROGMEM =
{
    { /*GSM 0x1B,*/ 0x0A, /*Unicode*/ 0x000C },
    { /*GSM 0x1B,*/ 0x14, /*Unicode*/ 0x005E },
    { /*GSM 0x1B,*/ 0x28, /*Unicode*/ 0x007B },
    { /*GSM 0x1B,*/ 0x29, /*Unicode*/ 0x007D },
    { /*GSM 0x1B,*/ 0x2F, /*Unicode*/ 0x005C },
    { /*GSM 0x1B,*/ 0x3C, /*Unicode*/ 0x005B },
    { /*GSM 0x1B,*/ 0x3D, /*Unicode*/ 0x007E },
    { /*GSM 0x1B,*/ 0x3E, /*Unicode*/ 0x005D },
    { /*GSM 0x1B,*/ 0x40, /*Unicode*/ 0x007C },
    { /*GSM 0x1B,*/ 0x65, /*Unicode*/ 0x20AC },
};

SoftwareSerial ModemSerial(MODEM_SERIAL_RX_PIN, MODEM_SERIAL_TX_PIN);

//|XXX ==============================================================================================================
Stream* XXX_ModemSerialPtr = &ModemSerial;
#define ModemSerial (*XXX_ModemSerialPtr)
struct XXX_ModemSerialScope
{
    Stream* const oldModemSerialPtr;
    XXX_ModemSerialScope(Stream& stream) : oldModemSerialPtr(XXX_ModemSerialPtr) { XXX_ModemSerialPtr = &stream; }
    ~XXX_ModemSerialScope() { XXX_ModemSerialPtr = oldModemSerialPtr; }
};

//|XXX
void XXX_printChar(int value)
{
    if (value < 0x10) { Serial.print(F("\\x0")); Serial.print(value, HEX); }
    else if (value < 0x20) { Serial.print(F("\\x")); Serial.print(value, HEX); }
    else { Serial.print((char)value); }
}
void XXX_printAndDiscardChar(Stream& stream)
{
    XXX_printChar(stream.read());
}

template<typename value_t>
void XXX_printError(const __FlashStringHelper* prefix, value_t value)
{
    Serial.print(F("Error: "));
    Serial.print(prefix);
    Serial.print(F(" = 0x"));
    Serial.println(value, HEX);
}
void XXX_printError(const __FlashStringHelper* message)
{
    Serial.print(F("Error: "));
    Serial.println(message);
}

class FlashStringStream : public Stream
{
public:
    FlashStringStream(const __FlashStringHelper* str)
        : ptr(reinterpret_cast<PGM_P>(str)) {}

    virtual int available() override { return pgm_read_byte(ptr) != 0; }
    virtual int read() override { unsigned char data = pgm_read_byte(ptr); if (data == 0) { return -1; } else { ++ptr; return data; } };
    virtual int peek() override { unsigned char data = pgm_read_byte(ptr); if (data == 0) { return -1; } else { return data; } };
    virtual size_t write(uint8_t) override { return 0; }
private:
    PGM_P ptr;
};
//|XXX ==============================================================================================================

unsigned long ModemTimeoutStart = 0;
unsigned long ModemTimeout = 0;

void startModemTimeout(unsigned long timeout)
{
    ModemTimeoutStart = millis();
    ModemTimeout = timeout;
}

bool isModemTimeout()
{
    return millis() - ModemTimeoutStart >= ModemTimeout;
}

void discardRead(Stream& stream)
{
    while (stream.available())
    {
        stream.read();
        delay(1);
    }
    delay(100);
}

template<typename command_char_t>
void sendCommandToModem(const command_char_t* command, unsigned long responseTimeout)
{
    ModemSerial.println(command);
    startModemTimeout(responseTimeout);
}

template<typename command_char_t, typename arguments_char_t>
void sendCommandToModem(const command_char_t* command, const arguments_char_t* arguments, unsigned long responseTimeout)
{
    ModemSerial.print(command);
    ModemSerial.println(arguments);
    startModemTimeout(responseTimeout);
}

ModemResult parseResultFromModem()
{
    char resultCode[2];

    if (readDigitsFromModem(resultCode, sizeof(resultCode)) &&
        skipCharacterFromModem('\r'))
    {
        return static_cast<ModemResult>(resultCode[0] - '0');
    }

    return ModemResult::None;
}

size_t readDigitsFromModem(char* buffer, size_t bufferSize)
{
    return readCharactersFromModemWhile(buffer, bufferSize, [](char character) { return character >= '0' && character <= '9'; });
}

size_t readLineFromModem(char* buffer, size_t bufferSize, bool skipWhitespace = true, bool skipEndOfLine = true)
{
    if (skipWhitespace)
    {
        while (skipCharacterFromModem('\t') ||
               skipCharacterFromModem(' '));
    }

    size_t bufferLength = readCharactersFromModemUntil(buffer, bufferSize, '\r');

    if (skipEndOfLine)
    {
        skipCharacterFromModem('\r');
        skipCharacterFromModem('\n');
    }

    return bufferLength;
}

size_t readCharactersFromModemUntil(char* buffer, size_t bufferSize, char stopCharacter)
{
    return readCharactersFromModemWhile(buffer, bufferSize, [stopCharacter](char character) { return character != stopCharacter; });
}

template<typename char_predicate_t>
size_t readCharactersFromModemWhile(char* buffer, size_t bufferSize, char_predicate_t isCharacterToRead)
{
    size_t bufferOffset = 0;
    size_t bufferOffsetEnd = bufferSize - 1;

    while (bufferOffset < bufferOffsetEnd)
    {
        if (readCharacterFromModem(buffer[bufferOffset], isCharacterToRead))
        {
            bufferOffset++;
            continue;
        }

        break;
    }

    buffer[bufferOffset] = '\0';
    return bufferOffset;
}

template<typename char_predicate_t>
bool readCharacterFromModem(char& character, char_predicate_t isCharacterToRead)
{
    while (!isModemTimeout())
    {
        int maybeCharacter = ModemSerial.peek();

        if (maybeCharacter >= 0)
        {
            if (!isCharacterToRead(maybeCharacter))
            {
                break;
            }

            ModemSerial.read();
            character = static_cast<char>(maybeCharacter);
            return true;
        }
    }

    return false;
}

bool skipEndOfLineFromModem()
{
    return skipCharacterFromModem('\r') &&
           skipCharacterFromModem('\n');
}

bool skipCharactersFromModemUntil(char stopCharacter)
{
    return skipCharactersFromModemWhile([stopCharacter](char character) { return character != stopCharacter; });
}

template<typename char_predicate_t>
bool skipCharactersFromModemWhile(char_predicate_t isCharacterToSkip)
{
    bool skipped = false;

    while (skipCharacterFromModem(isCharacterToSkip))
    {
        skipped = true;
    }

    return skipped;
}

bool skipCharactersFromModem(const __FlashStringHelper* charactersToSkip)
{
    PGM_P charactersToSkipPtr = reinterpret_cast<PGM_P>(charactersToSkip);

    while (true)
    {
        char characterToSkip = pgm_read_byte(charactersToSkipPtr++);

        if (characterToSkip == '\0')
        {
            return true;
        }

        if (!skipCharacterFromModem(characterToSkip))
        {
            return false;
        }
    }
}

bool skipCharacterFromModem(char characterToSkip)
{
    return skipCharacterFromModem([characterToSkip](char character) { return character == characterToSkip; });
}

template<typename char_predicate_t>
bool skipCharacterFromModem(char_predicate_t isCharacterToSkip)
{
    while (!isModemTimeout())
    {
        int maybeCharacter = ModemSerial.peek();

        if (maybeCharacter >= 0)
        {
            if (!isCharacterToSkip(maybeCharacter))
            {
                break;
            }

            ModemSerial.read();
            return true;
        }
    }

    return false;
}

void discardLineFromModem()
{
    skipCharactersFromModemUntil('\r');
    skipCharacterFromModem('\r');
    skipCharacterFromModem('\n');
}

void discardResponseFromModem()
{
    for (uint8_t numResponseLines = 2; numResponseLines != 0; --numResponseLines)
    {
        char potentialResultCode[2+1];

        if (readCharactersFromModemUntil(potentialResultCode, sizeof(potentialResultCode), '\r') == 0)
        {
            return;
        }

        if (potentialResultCode[0] >= '0' &&
            potentialResultCode[0] <= '9' &&
            potentialResultCode[1] == '\0')
        {
            skipCharacterFromModem('\r');
            return;
        }

        discardLineFromModem();
    }
}

wchar_t lookupUnicodeForGSM(unsigned char character, bool isExtended)
{
    if (isExtended)
    {
        for (const Mapping<char, wchar_t>& mapping : GSMToUnicodeExtension)
        {
            char characterToMap = pgm_read_byte(&mapping.from);
            if (characterToMap == character)
            {
                wchar_t codepoint = pgm_read_word(&mapping.to);
                return codepoint;
            }
        }
    }
    else if (character <= '\x7F')
    {
        wchar_t codepoint = pgm_read_word(&GSMToUnicode[character]);
        return codepoint;
    }

    return 0xFFFD; // REPLACEMENT CHARACTER
}

void printEncoded(Stream& stream, const char* buffer, size_t bufferSize, Encoding encoding = Encoding::UTF8)
{
    switch (encoding)
    {
        case Encoding::UTF8:
        {
            for (size_t bufferOffset = 0; bufferOffset < bufferSize; ++bufferOffset)
            {
                char character = buffer[bufferOffset];

                if (character == '\0')
                {
                    break;
                }

                stream.write(character);
            }
        }
        return;

        case Encoding::UCS2LE:
        {
            size_t bufferOffsetEnd = bufferSize & 0xFFFE;

            for (size_t bufferOffset = 0; bufferOffset < bufferOffsetEnd; bufferOffset += sizeof(wchar_t))
            {
                wchar_t codepoint =
                    static_cast<unsigned char>(buffer[bufferOffset]) |
                    static_cast<unsigned char>(buffer[bufferOffset + 1]) << 8;

                if (codepoint == '\0')
                {
                    break;
                }

                printUnicode(stream, codepoint);
            }
        }
        break;

        case Encoding::UCS2BE:
        {
            size_t bufferOffsetEnd = bufferSize & 0xFFFE;

            for (size_t bufferOffset = 0; bufferOffset < bufferOffsetEnd; bufferOffset += sizeof(wchar_t))
            {
                wchar_t codepoint =
                    static_cast<unsigned char>(buffer[bufferOffset]) << 8 |
                    static_cast<unsigned char>(buffer[bufferOffset + 1]);
                
                if (codepoint == '\0')
                {
                    break;
                }

                printUnicode(stream, codepoint);
            }
        }
        break;

        case Encoding::GSMBYTES:
        {
            bool isExtended = false;

            for (size_t bufferOffset = 0; bufferOffset < bufferSize; ++bufferOffset)
            {
                unsigned char character = buffer[bufferOffset];

                if (character > '\x7F')
                {
                    break;
                }

                if (character == '\x1B')
                {
                    isExtended = true;
                    continue;
                }

                wchar_t codepoint = lookupUnicodeForGSM(character, isExtended);
                printUnicode(stream, codepoint);

                isExtended = false;
            }
        }
        break;

        default: break;
    }
}

void printlnEncoded(Stream& stream, const char* buffer, size_t bufferSize, Encoding encoding = Encoding::UTF8)
{
    printEncoded(stream, buffer, bufferSize, encoding);
    stream.write('\n');
}

void printUnicode(Stream& stream, wchar_t codepoint)
{
    if (codepoint <= 0x7F)
    {
        stream.write(codepoint);
    }
    else if (codepoint <= 0x7FF)
    {
        stream.write(0xC0 | (codepoint >> 6));
        stream.write(0x80 | (codepoint & 0x3F));
    }
    else
    {
        stream.write(0xE0 | (codepoint >> 12));
        stream.write(0x80 | ((codepoint >> 6) & 0x3F));
        stream.write(0x80 | (codepoint & 0x3F));
    }
}

bool queryIMEI()
{
    char imei[16+1];

    sendCommandToModem(F("AT+GSN"), 300);

    if (readDigitsFromModem(imei, sizeof(imei)) &&
        skipEndOfLineFromModem() &&
        parseResultFromModem() == ModemResult::OK)
    {
        // Success: received IMEI
    }
    else
    {
        discardResponseFromModem();
        return false;
    }

    Serial.print(F("GSM: IMEI "));
    Serial.print(imei);
    Serial.println();
    return true;
}

template<typename pin_char_t>
bool enterSIMPIN(const pin_char_t* pin)
{
    char pinStatus[10+1];

    sendCommandToModem(F("AT+CPIN?"), 5000);

    if (skipCharactersFromModem(F("+CPIN:")) &&
        readLineFromModem(pinStatus, sizeof(pinStatus)) &&
        parseResultFromModem() == ModemResult::OK)
    {
        // Success: received PIN status
    }
    else
    {
        discardResponseFromModem();
        return false;
    }

    if (strcmp_P(pinStatus, PSTR("READY")) == 0)
    {
        Serial.println(F("GSM: PIN not required"));
        return true;
    }

    if (strcmp_P(pinStatus, PSTR("SIM PIN")) != 0)
    {
        Serial.print(F("GSM: "));
        Serial.print(pinStatus);
        Serial.println(F(" required (not supported)"));
        return false;
    }

    if (pin == nullptr)
    {
        Serial.println(F("GSM: PIN required but not configured"));
        return false;
    }
    
    Serial.print(F("GSM: PIN required, entering: "));
    Serial.println(pin);

    sendCommandToModem(F("AT+CPIN="), pin, 5000);

    if (skipCharactersFromModem(F("+CPIN:")) &&
        readLineFromModem(pinStatus, sizeof(pinStatus)) &&
        parseResultFromModem() == ModemResult::OK)
    {
        // Success: received PIN status
    }
    else
    {
        discardResponseFromModem();
        return false;
    }

    if (strcmp_P(pinStatus, PSTR("READY")) != 0)
    {
        Serial.print(F("GSM: PIN not accepted, "));
        Serial.print(pinStatus);
        Serial.println(F(" required (not supported)"));
        return false;
    }

    Serial.println(F("GSM: PIN accepted"));
    return true;
}

bool setupReceiveSMS()
{
    // Configure new SMS message indications
    //   <mode>=3: forward unsolicited result directly to the TE
    //   <mt>=2: route SMS directly to the TE using +CMT
    //   <bm>=2: route CBM directly to the TE using +CBM
    
    sendCommandToModem(F("AT+CNMI=3,2,2"), 300);

    if (parseResultFromModem() == ModemResult::OK)
    {
        // Success: configured SMS receive option
    }
    else
    {
        discardResponseFromModem();
        return false;
    }

    return true;
}

bool isHexDigit(char character)
{
    return (character >= '0' && character <= '9') ||
           (character >= 'A' && character <= 'F') ||
           (character >= 'a' && character <= 'f');
}

template<typename numeric_t>
numeric_t parseHexFromModem(numeric_t resultOnError = static_cast<numeric_t>(-1), size_t numHexDigits = 2 * sizeof(numeric_t))
{
    numeric_t result = 0;

    while (numHexDigits > 0)
    {
        char hexDigit;

        if (readCharacterFromModem(hexDigit, isHexDigit))
        {
            numHexDigits--;

            // 0..9: 00110000..00111001 (digit & 0x0F)
            // A..F: 01000001..01000110 (digit & 0x0F + 9)
            // a..f: 01100001..01100110 (digit & 0x0F + 9)
            result <<= 4;
            result |= (hexDigit < 'A') ? (hexDigit & 0x0F) : (hexDigit & 0x0F) + 9;

            continue;
        }

        return resultOnError;
    }

    return result;
}

bool skipHexFromModem(size_t numHexDigits)
{
    while (numHexDigits > 0)
    {
        if (skipCharacterFromModem(isHexDigit))
        {
            numHexDigits--;
            continue;
        }

        return false;
    }

    return true;
}

uint8_t parseGSMAlphaFromModem(char* buffer, size_t bufferSize, uint8_t charactersToParse)
{
    char character = '\0';
    char characterRemainder = '\0';

    uint8_t characterPosition = 0;

    size_t bufferOffset = 0;
    size_t bufferOffsetEnd = bufferSize - 1;

    for (; charactersToParse > 0; --charactersToParse)
    {
        uint8_t octet = parseHexFromModem<uint8_t>();

        switch (characterPosition++)
        {
            case 0:
                character = octet & 0x7F;
                characterRemainder = octet >> 7;
                break;

            case 1:
                character = ((octet << 1) & 0x7F) | characterRemainder;
                characterRemainder = octet >> 6;
                break;

            case 2:
                character = ((octet << 2) & 0x7F) | characterRemainder;
                characterRemainder = octet >> 5;
                break;

            case 3:
                character = ((octet << 3) & 0x7F) | characterRemainder;
                characterRemainder = octet >> 4;
                break;

            case 4:
                character = ((octet << 4) & 0x7F) | characterRemainder;
                characterRemainder = octet >> 3;
                break;

            case 5:
                character = ((octet << 5) & 0x7F) | characterRemainder;
                characterRemainder = octet >> 2;
                break;

            case 6:
                character = ((octet << 6) & 0x7F) | characterRemainder;
                characterRemainder = octet >> 1;
                break;
        }

        if (bufferOffset < bufferOffsetEnd)
        {
            buffer[bufferOffset++] = character;
        }

        if (characterPosition == 7)
        {
            if (charactersToParse == 1)
            {
                break;
            }

            if (bufferOffset < bufferOffsetEnd)
            {
                buffer[bufferOffset++] = characterRemainder;
            }

            characterPosition = 0;
            charactersToParse--;
        }
    }

    buffer[bufferOffset] = '\xFF';

    return isModemTimeout() ? 0 : static_cast<uint8_t>(bufferOffset);
}

bool parseSMSPDUFromModem(char* senderBuffer, size_t senderBufferSize, uint8_t& senderLengthOut, char* messageBuffer, size_t messageBufferSize, Encoding& messageEncodingOut, uint8_t& messageLengthOut)
{
    senderBuffer[0] = '\xFF';
    senderLengthOut = 0;

    messageBuffer[0] = '\xFF';
    messageEncodingOut = Encoding::Unknown;
    messageLengthOut = 0;

    // Skip SMSC address:
    //   byte  0:    number of octets
    //   bytes 1..n: SMS message center address
    {
        uint8_t dispatcherAddressLength = parseHexFromModem<uint8_t>();

        if (dispatcherAddressLength > 12 || !skipHexFromModem(2 * dispatcherAddressLength))
        {
            return false;
        }
    }

    bool hasUserDataHeader = false;

    // Read message header:
    //   bits 0..1: TP-MTI  (message type indicator)
    //   bit  2:    TP-MMS  (more messages to send)
    //   bit  5:    TP-SRI  (status report indication)
    //   bit  6:    TP-UDHI (user data header indicator)
    //   bit  7:    TP-RP   (reply path)
    {
        uint8_t header = parseHexFromModem<uint8_t>();

        const uint8_t TP_MTI_MASK        = 0x03;
        const uint8_t TP_MTI_SMS_DELIVER = 0x00;

        const uint8_t TP_UDHI_MASK       = 0x40;
        const uint8_t TP_UDHI_PRESENT    = 0x40;

        if (header == 0xFF || (header & TP_MTI_MASK) != TP_MTI_SMS_DELIVER)
        {
            return false;
        }

        hasUserDataHeader = (header & TP_UDHI_MASK) == TP_UDHI_PRESENT;
    }

    // Read TP-OA (originating address):
    //   byte  0:            number of useful semi-octets within the value field
    //   byte  1, bit  7:    type of address header fixed bit
    //   byte  1, bits 4..6: type of number
    //   byte  1, bits 0..3: numbering plan identification
    //   byte  1, bits 4..6: type of number
    //   byte  1, bit  7:    always set
    //   bytes 2..n:         originating address encoded as per type of number
    {
        uint8_t senderAddressSemiOctets = parseHexFromModem<uint8_t>();
        uint8_t senderAddressType = parseHexFromModem<uint8_t>();

        if (senderAddressSemiOctets > 20)
        {
            return false;
        }

        const uint8_t ADDRESS_TYPE_NUMBER_MASK            = 0x70;
        const uint8_t ADDRESS_TYPE_NUMBER_INTERNATIONAL   = 0x10;
        const uint8_t ADDRESS_TYPE_NUMBER_NATIONAL        = 0x20;
        const uint8_t ADDRESS_TYPE_NUMBER_NETWORKSPECIFIC = 0x30;
        const uint8_t ADDRESS_TYPE_NUMBER_SUBSCRIBER      = 0x40;
        const uint8_t ADDRESS_TYPE_NUMBER_ALPHANUMERIC    = 0x50;
        const uint8_t ADDRESS_TYPE_NUMBER_ABBREVIATED     = 0x60;

        switch (senderAddressType & ADDRESS_TYPE_NUMBER_MASK)
        {
            case ADDRESS_TYPE_NUMBER_INTERNATIONAL:
            {
                size_t senderBufferOffset = 0;
                size_t senderBufferOffsetEnd = senderBufferSize - 1;

                if (senderBufferOffset < senderBufferOffsetEnd)
                {
                    senderBuffer[senderBufferOffset++] = '+';
                }

                for (uint8_t senderAddressOctets = (senderAddressSemiOctets + 1) >> 1; senderAddressOctets > 0; --senderAddressOctets)
                {
                    uint8_t senderAddressOctet = parseHexFromModem<uint8_t>();

                    if (senderBufferOffset < senderBufferOffsetEnd)
                    {
                        senderBuffer[senderBufferOffset++] = '0' + (senderAddressOctet & 0x0F);
                    }

                    senderAddressOctet >>= 4;

                    if (senderBufferOffset < senderBufferOffsetEnd && senderAddressOctet != 0x0F)
                    {
                        senderBuffer[senderBufferOffset++] = '0' + senderAddressOctet;
                    }
                }

                senderBuffer[senderBufferOffset] = '\xFF';

                senderLengthOut = static_cast<uint8_t>(senderBufferOffset);
            }
            break;

            case ADDRESS_TYPE_NUMBER_NATIONAL:
            case ADDRESS_TYPE_NUMBER_NETWORKSPECIFIC:
            case ADDRESS_TYPE_NUMBER_SUBSCRIBER:
            case ADDRESS_TYPE_NUMBER_ABBREVIATED:
            {
                char* senderCharacterPtr = senderBuffer;
                char* senderCharacterPtrEnd = senderBuffer + senderBufferSize - 1;

                for (uint8_t senderAddressOctets = (senderAddressSemiOctets + 1) >> 1; senderAddressOctets > 0; --senderAddressOctets)
                {
                    uint8_t senderAddressOctet = parseHexFromModem<uint8_t>();

                    if (senderCharacterPtr < senderCharacterPtrEnd)
                    {
                        *senderCharacterPtr++ = '0' + (senderAddressOctet & 0x0F);
                    }

                    senderAddressOctet >>= 4;

                    if (senderCharacterPtr < senderCharacterPtrEnd && senderAddressOctet != 0x0F)
                    {
                        *senderCharacterPtr++ = '0' + senderAddressOctet;
                    }
                }

                *senderCharacterPtr = '\xFF';

                senderLengthOut = static_cast<uint8_t>(senderCharacterPtr - senderBuffer);
            }
            break;

            case ADDRESS_TYPE_NUMBER_ALPHANUMERIC:
            {
                senderLengthOut = parseGSMAlphaFromModem(senderBuffer, senderBufferSize, (senderAddressSemiOctets * 4) / 7);

                if (senderLengthOut == 0)
                {
                    return false;
                }
            }
            break;

            default:
            {
                skipHexFromModem((senderAddressSemiOctets + 1) & 0xFE);
            }
            break;
        }
    }

    // Skip TP-PID (protocol identifier)
    {
        if (!skipHexFromModem(2))
        {
            return false;
        }
    }

    uint8_t messageCoding;

    const uint8_t TP_DCS_GSM7BIT = 0x00;
    const uint8_t TP_DCS_UCS2BE  = 0x08;

    // Read TP-DCS (data coding scheme)
    {
        messageCoding = parseHexFromModem<uint8_t>();

        if (messageCoding != TP_DCS_GSM7BIT &&
            messageCoding != TP_DCS_UCS2BE)
        {
            return false;
        }
    }

    // Skip TP-SCTS (service centre time stamp):
    //   byte 0: year     (semi-octets) since 2000
    //   byte 1: month    (semi-octets)
    //   byte 2: day      (semi-octets)
    //   byte 3: hour     (semi-octets)
    //   byte 4: minute   (semi-octets)
    //   byte 5: second   (semi-octets)
    //   byte 6: timezone (semi-octets) in 15 minute intervals
    {
        if (!skipHexFromModem(2*7))
        {
            return false;
        }
    }

    // Read message:
    //   byte  0:    TP-UDL (user data length)
    //   bytes 1..n: TP-UD  (user data)
    {
        uint8_t messageCharactersToRead = parseHexFromModem<uint8_t>();

        if (messageCharactersToRead > 160)
        {
            return false;
        }

        if (hasUserDataHeader)
        {
            uint8_t userDataHeaderLength = parseHexFromModem<uint8_t>();

            if (userDataHeaderLength + 1 > messageCharactersToRead)
            {
                return false;
            }

            // Skip user data header:
            //   byte  0:    information element identifier
            //   byte  1:    length of information element (octets)
            //   bytes 2..n: information element data

            // For concatenated messages:
            //   byte  0:    0x00 = concatenated short messages
            //   byte  1:    0x03
            //   byte  2:    message reference number
            //   byte  3:    maximum number of short messages in concatenated message
            //   byte  4:    sequence number of current short message in concatenated message, starting at 1

            skipHexFromModem(2 * userDataHeaderLength);

            messageCharactersToRead -= userDataHeaderLength + 1;
        }

        switch (messageCoding)
        {
            case TP_DCS_GSM7BIT:
            {
                messageEncodingOut = Encoding::GSMBYTES;
                messageLengthOut = parseGSMAlphaFromModem(messageBuffer, messageBufferSize, messageCharactersToRead);

                if (messageLengthOut == 0)
                {
                    return false;
                }
            }
            break;

            case TP_DCS_UCS2BE:
            {
                messageEncodingOut = Encoding::UCS2BE;

                uint8_t messageBufferOffset = 0;
                uint8_t messageBufferOffsetEnd = messageBufferSize - sizeof(wchar_t);

                for (; messageCharactersToRead > 0; --messageCharactersToRead)
                {
                    uint8_t messageByte = parseHexFromModem<uint8_t>();

                    if (messageBufferOffset < messageBufferOffsetEnd)
                    {
                        messageBuffer[messageBufferOffset++] = messageByte;
                    }
                }

                reinterpret_cast<wchar_t&>(messageBuffer[messageBufferOffset]) = '\0';

                messageLengthOut = messageBufferOffset;
            }
            break;
        }
    }

    return !isModemTimeout();
}

//|XXX
#undef ModemSerial

void setup()
{
    Serial.begin(57600);
    ModemSerial.begin(9600);

    discardRead(Serial);
    discardRead(ModemSerial);

    pinMode(MODEM_RESET_PIN, OUTPUT);
    digitalWrite(MODEM_RESET_PIN, HIGH);
    pinMode(MODEM_POWER_PIN, OUTPUT);
    digitalWrite(MODEM_POWER_PIN, HIGH);
    pinMode(MODEM_STATUS_PIN, INPUT);

    delay(100);

    Serial.flush();
    ModemSerial.flush();

    delay(1000);

    Serial.println(F("SMSForwarder startup"));
    Serial.println(F("GSM: Powering on"));

    delay(100);

    if (!digitalRead(MODEM_STATUS_PIN))
    {
        discardRead(ModemSerial);
        digitalWrite(MODEM_POWER_PIN, LOW);
        delay(1000);
        digitalWrite(MODEM_POWER_PIN, HIGH);
        delay(8000);
    }

    Serial.println(F("GSM: Powered on"));

    // Disable echo and verbose results
    {
        sendCommandToModem(F("ATE0"), 300);
        discardResponseFromModem();
        sendCommandToModem(F("ATV0"), 300);
        discardResponseFromModem();
    }

    if (queryIMEI() &&
        enterSIMPIN(F(SIM_PIN)) &&
        setupReceiveSMS())
    {
        Serial.println(F("SMSForwarder startup successful"));
    }
    else
    {
        Serial.println(F("SMSForwarder startup failed"));

        while (true)
        {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(500);
            digitalWrite(LED_BUILTIN, LOW);
            delay(500);
        }
    }

    //|XXX
    if (true)
    {
        {
            startModemTimeout(300);
            
            FlashStringStream PDUStream(F("0791947101670000040C9194713001746200005220421191954019C8329BFD06D1D1657939A49896C76F3719C44EBBCB21\r\n"));
            XXX_ModemSerialScope PDUStreamScope(PDUStream);

            char sender[20+1];
            uint8_t senderLength;

            char message[160+1];
            Encoding messageEncoding;
            uint8_t messageLength;

            if (parseSMSPDUFromModem(sender, sizeof(sender), senderLength, message, sizeof(message), messageEncoding, messageLength))
            {
                Serial.print(F("Test: SMS sender: "));
                printlnEncoded(Serial, sender, sizeof(sender), Encoding::GSMBYTES);
                Serial.print(F("Test: SMS message (encoding "));
                Serial.print(static_cast<uint8_t>(messageEncoding));
                Serial.print(F("): "));
                printlnEncoded(Serial, message, sizeof(message), messageEncoding);
            }
            else
            {
                Serial.println(F("Test: Failed to parse SMS PDU"));
            }
        }
        {
            startModemTimeout(300);
            
            FlashStringStream PDUStream(F("0791947101670000040C919471300174620000522042119195405661BA0E0022BFD9ECB05C2700CDE73A0F681C9697E9BA0D05249687C7E5B96E83DAA440E2F078ADDBBC40627978BC2ED3E7BA0D6FE303D1D36C7259B7E981E0697859B70182CA75F95BB72903\r\n"));
            XXX_ModemSerialScope PDUStreamScope(PDUStream);

            char sender[20+1];
            uint8_t senderLength;

            char message[160+1];
            Encoding messageEncoding;
            uint8_t messageLength;

            if (parseSMSPDUFromModem(sender, sizeof(sender), senderLength, message, sizeof(message), messageEncoding, messageLength))
            {
                Serial.print(F("Test: SMS sender: "));
                printlnEncoded(Serial, sender, sizeof(sender), Encoding::GSMBYTES);
                Serial.print(F("Test: SMS message (encoding "));
                Serial.print(static_cast<uint8_t>(messageEncoding));
                Serial.print(F("): "));
                printlnEncoded(Serial, message, sizeof(message), messageEncoding);
            }
            else
            {
                Serial.println(F("Test: Failed to parse SMS PDU"));
            }
        }
        {
            startModemTimeout(300);
            
            FlashStringStream PDUStream(F("0791021197003899440ED0657A7A1E6687E93408610192016390004205000365030106440642062F002006270633062A064706440643062A0020064306440020062706440648062D062F0627062A0020062706440645062C06270646064A\r\n"));
            XXX_ModemSerialScope PDUStreamScope(PDUStream);

            char sender[20+1];
            uint8_t senderLength;

            char message[160+1];
            Encoding messageEncoding;
            uint8_t messageLength;

            if (parseSMSPDUFromModem(sender, sizeof(sender), senderLength, message, sizeof(message), messageEncoding, messageLength))
            {
                Serial.print(F("Test: SMS sender: "));
                printlnEncoded(Serial, sender, sizeof(sender), Encoding::GSMBYTES);
                Serial.print(F("Test: SMS message (encoding "));
                Serial.print(static_cast<uint8_t>(messageEncoding));
                Serial.print(F("): "));
                printlnEncoded(Serial, message, sizeof(message), messageEncoding);
            }
            else
            {
                Serial.println(F("Test: Failed to parse SMS PDU"));
            }
        }
        {
            startModemTimeout(300);
            
            FlashStringStream PDUStream(F("0791947101670000040C919471300174620008523030714172404600540065007300740020006D0069007400200041006B007A0065006E0074003A002000E100E0000A0055006D006C0061007500740065003A002000E400F600FC00C400D600DC\r\n"));
            XXX_ModemSerialScope PDUStreamScope(PDUStream);

            char sender[20+1];
            uint8_t senderLength;

            char message[160+1];
            Encoding messageEncoding;
            uint8_t messageLength;

            if (parseSMSPDUFromModem(sender, sizeof(sender), senderLength, message, sizeof(message), messageEncoding, messageLength))
            {
                Serial.print(F("Test: SMS sender: "));
                printlnEncoded(Serial, sender, sizeof(sender), Encoding::GSMBYTES);
                Serial.print(F("Test: SMS message (encoding "));
                Serial.print(static_cast<uint8_t>(messageEncoding));
                Serial.print(F("): "));
                printlnEncoded(Serial, message, sizeof(message), messageEncoding);
            }
            else
            {
                Serial.println(F("Test: Failed to parse SMS PDU"));
            }
        }
    }
}

void loop()
{
    int character = ModemSerial.read();

    if (character < 0)
    {
        return;
    }

    startModemTimeout(1000);

    if (character != '+')
    {
        discardLineFromModem();
        return;
    }

    char unsolicitedResultCode[9+1];

    if (readCharactersFromModemUntil(unsolicitedResultCode, sizeof(unsolicitedResultCode), ':'))
    {
        if (strcmp_P(unsolicitedResultCode, PSTR("CMT")) == 0)
        {
            Serial.println(F("GSM: Received SMS"));

            skipCharactersFromModemUntil('\r');
            skipEndOfLineFromModem();

            /*
            char line[2*163+1];

            readCharactersFromModemUntil(line, sizeof(line), '\r');
            discardLineFromModem();

            Serial.print(F("GSM: SMS PDU: "));
            Serial.println(line);

            return;
            /*/
            char sender[17+1];
            uint8_t senderLength;

            char message[160+1];
            Encoding messageEncoding;
            uint8_t messageLength;

            if (parseSMSPDUFromModem(sender, sizeof(sender), senderLength, message, sizeof(message), messageEncoding, messageLength))
            {
                Serial.print(F("GSM: SMS sender: "));
                printlnEncoded(Serial, sender, sizeof(sender), Encoding::GSMBYTES);
                Serial.print(F("GSM: SMS message: "));
                printlnEncoded(Serial, message, sizeof(message), messageEncoding);

                //|XXX enqueue forwarded SMS

                return;
            }
            else
            {
                discardLineFromModem();
                return;
            }
            //*/
        }

        Serial.print(F("GSM: Unsupported unsolicited result code: "));
        Serial.println(unsolicitedResultCode);
        return;
    }

    discardLineFromModem();
}
