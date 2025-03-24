// SMSForwarder
// 2025 michael@buschbeck.net

// Arduino Uno R3 (AVR)
// a-gsmII shield by itbrainpower.net

// Increase the serial RX buffer to ensure reliable communcation with the GSM modem:
// %LocalAppData%\Arduino15\packages\arduino\hardware\avr\1.8.6\boards.txt
// uno.build.extra_flags=-D_SS_MAX_RX_BUFF=128

#include <Arduino.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>

// for placement new
#include <new>

// for perfect forwarding
namespace std
{
    template <typename T>
    struct remove_reference {
        using type = T;
    };
    
    template <typename T>
    struct remove_reference<T&> {
        using type = T;
    };
    
    template <typename T>
    struct remove_reference<T&&> {
        using type = T;
    };
    
    template <typename T>
    T&& forward(typename remove_reference<T>::type& t) {
        return static_cast<T&&>(t);
    }
    
    template <typename T>
    T&& forward(typename remove_reference<T>::type&& t) {
        return static_cast<T&&>(t);
    }
}

#define MODEM_SERIAL_RX_PIN 2
#define MODEM_SERIAL_TX_PIN 3
#define MODEM_STATUS_PIN    5
#define MODEM_RESET_PIN     6
#define MODEM_POWER_PIN     7

struct EEPROMConfigSchema
{
    static constexpr unsigned long long ExpectedSchema =
        static_cast<unsigned long long>('S')       |
        static_cast<unsigned long long>('M') <<  8 |
        static_cast<unsigned long long>('S') << 16 |
        static_cast<unsigned long long>('F') << 24 |
        static_cast<unsigned long long>('r') << 32 |
        static_cast<unsigned long long>('w') << 40 |
        static_cast<unsigned long long>('d') << 48 |
        static_cast<unsigned long long>('r') << 56;

    static constexpr unsigned int ExpectedVersion = 1;

    // Stored configuration schema identifier and version
    unsigned long long schema;
    unsigned int version;

    // Stored SIM PIN:
    // Null-terminated string, may be empty
    char SIMPIN[4+1] = {0};

    // Number of stored forwarder destination phone numbers
    static constexpr uint8_t NumDestinations = 3;

    // Stored forwarder destination phone numbers:
    // Null-terminated strings, each may be empty
    char destinations[NumDestinations][17+1] = {0};
};

EEPROMConfigSchema EEPROMConfig;

struct DestinationState
{
    bool interactionInProgress = false;
};

DestinationState DestinationStates[EEPROMConfigSchema::NumDestinations];

struct ModemSerialState
{
    enum : int { NoData      = -1 };
    enum : int { EndOfStream = -2 };
};

enum struct ModemRuntimeState : uint8_t
{
    Uninitialized = 0,
    Authorize     = 1,
    Active        = 2,
};

enum struct ModemCommandState : uint8_t
{
    Uninitialized          = 0,
    ReadyToSendCommand     = 1,
    ReadyToProcessResponse = 2,
};

struct ModemCommand
{
    // Send modem command
    // Return true if response expected, false if not
    virtual bool sendCommand() = 0;

    // Process modem response to sent command
    // Only called when prior command sent indicated that response is expected
    // Return true if expected response received, false if not (must rollback read buffer)
    virtual bool tryProcessResponse() = 0;

    // If available, advance to the next command to send
    // Return true if there is another command to send, false if not
    virtual bool advanceToNextCommand() { return false; }

    // Clean up anything that was set up specifically for this command
    virtual ~ModemCommand() = default;

    // State of this command
    // Set this to ReadyToSendCommand after initialization is complete
    ModemCommandState state = ModemCommandState::Uninitialized;

    // ModemTimeoutTime initialized after command was sent
    unsigned long responseTimeoutTime = 0;
};

enum struct ModemResult : int
{
    None = -1,

    // Standard Hayes result codes
    OK         = 0,
    Connect    = 1,
    Ring       = 2,
    NoCarrier  = 3,
    Error      = 4,
    NoDialtone = 6,
    Busy       = 7,
    NoAnswer   = 8,
    Proceeding = 9,

    // Quectel file access result codes
    FS_InvalidInputValue                          =  3765,
    FS_NonExistentAddress                         =  3915,
    FS_UFSStorageFull                             =  3916,
    FS_DriveFull                                  =  3917,
    FS_DriveError                                 =  3918,
    FS_FileNotFound1                              =  3919,
    FS_InvalidFileName                            =  3920,
    FS_FileAlreadyExisted                         =  3921,
    FS_FailedToCreateFile                         =  3922,
    FS_FailedToWriteFile                          =  3923,
    FS_FailedToOpenFile                           =  3924,
    FS_FailedToReadFile                           =  3925,
    FS_ExceedMaxLength                            =  4000,
    FS_OpenFileFail                               =  4001,
    FS_WriteFileFail                              =  4002,
    FS_GetSizeFail                                =  4003,
    FS_ReadFail                                   =  4004,
    FS_ListFileFail                               =  4005,
    FS_DeleteFileFail                             =  4006,
    FS_GetDiskInfoFail                            =  4007,
    FS_NoSpace                                    =  4008,
    FS_TimeOut                                    =  4009,
    FS_FileNotFound2                              =  4010,
    FS_FileTooLarge                               =  4011,
    FS_FileAlreadyExist                           =  4012,
    FS_InvalidParameter                           =  4013,
    FS_DriverError                                =  4014,
    FS_CreateFail                                 =  4015,
    FS_AccessDenied                               =  4016,

    // Quectel +CME ERROR result codes (shifted by 10000)
    CME                                           = 10000,
    CME_PhoneFailure                              = 10000,
    CME_NoConnectionToPhone                       = 10001,
    CME_PhoneAdaptorLinkReserved                  = 10002,
    CME_OperationNotAllowed                       = 10003,
    CME_OperationNotSupported                     = 10004,
    CME_PHSIMPINRequired                          = 10005,
    CME_PHFSIMPINRequired                         = 10006,
    CME_PHFSIMPUKRequired                         = 10007,
    CME_SIMNotInserted                            = 10010,
    CME_SIMPINRequired                            = 10011,
    CME_SIMPUKRequired                            = 10012,
    CME_SIMFailure                                = 10013,
    CME_SIMBusy                                   = 10014,
    CME_SIMWrong                                  = 10015,
    CME_IncorrectPassword                         = 10016,
    CME_SIMPIN2Required                           = 10017,
    CME_SIMPUK2Required                           = 10018,
    CME_MemoryFull                                = 10020,
    CME_InvalidIndex                              = 10021,
    CME_NotFound                                  = 10022,
    CME_MemoryFailure                             = 10023,
    CME_TextStringTooLong                         = 10024,
    CME_InvalidCharactersInTextString             = 10025,
    CME_DialStringTooLong                         = 10026,
    CME_InvalidCharactersInDialString             = 10027,
    CME_NoNetworkService                          = 10030,
    CME_NetworkTimeout                            = 10031,
    CME_NetworkNotAllowedEmergencyCallsOnly       = 10032,
    CME_NetworkPersonalizationPINRequired         = 10040,
    CME_NetworkPersonalizationPUKRequired         = 10041,
    CME_NetworkSubsetPersonalizationPINRequired   = 10042,
    CME_NetworkSubsetPersonalizationPUKRequired   = 10043,
    CME_ServiceProviderPersonalizationPINRequired = 10044,
    CME_ServiceProviderPersonalizationPUKRequired = 10045,
    CME_CorporatePersonalizationPINRequired       = 10046,
    CME_CorporatePersonalizationPUKRequired       = 10047,
    CME_IllegalMS                                 = 10103,
    CME_IllegalME                                 = 10106,
    CME_GPRSServicesNotAllowed                    = 10107,
    CME_PLMNNotAllowed                            = 10111,
    CME_LocationAreaNotAllowed                    = 10112,
    CME_RoamingNotAllowedInThisLocationArea       = 10113,
    CME_ServiceOptionNotSupported                 = 10132,
    CME_RequestedServiceOptionNotSubscribed       = 10133,
    CME_ServiceOptionTemporarilyOutOfOrder        = 10134,
    CME_UnspecifiedGPRSError                      = 10148,
    CME_PDPAuthenticationFailure                  = 10149,
    CME_InvalidMobileClass                        = 10150,
    CME_LinkNSSPPersonPINRequired                 = 10151,
    CME_LinkNSSPPersonPUKRequired                 = 10152,
    CME_LinkSIMCPersonPINRequired                 = 10153,
    CME_LinkSIMCPersonPUKRequired                 = 10154,
    CME_CommandConflict                           = 10302,
    CME_UnrecognizedCommand                       = 10601,
    CME_ReturnError                               = 10602,
    CME_SyntaxError                               = 10603,
    CME_Unspecified                               = 10604,
    CME_DataTransferAlready                       = 10605,
    CME_ActionAlready                             = 10606,
    CME_NotATCommand                              = 10607,
    CME_MultiCommandTooLong                       = 10608,
    CME_AbortCOPS                                 = 10609,
    CME_NoCallDisconnect                          = 10610,
    CME_UnreadRecordsOnSIM                        = 13513,
    CME_PSBusy                                    = 13515,
    CME_CouldNotReadSMSParametersFromSIM          = 13516,
    CME_SMNotReady                                = 13517,
    CME_InvalidParameter                          = 13518,
    CME_CSCSModeNotFound                          = 13738,
    CME_CPOLOperationFormatWrong                  = 13742,
    CME_InvalidInputValue                         = 13765,
    CME_UnableToGetControl                        = 13769,
    CME_CallSetupInProgress                       = 13771,
    CME_SIMPoweredDown                            = 13772,
    CME_InvalidCFUNState                          = 13773,
    CME_InvalidARFCN                              = 13774,
    CME_ThePinIsNotInGPIOMode                     = 13775,

    // Quectel +CMS ERROR result codes (shifted by 20000)
    CMS                                           = 20000,
    CMS_MEFailure                                 = 20300,
    CMS_SMSMEReserved                             = 20301,
    CMS_OperationNotAllowed                       = 20302,
    CMS_OperationNotSupported                     = 20303,
    CMS_InvalidPDUMode                            = 20304,
    CMS_InvalidTextMode                           = 20305,
    CMS_SIMNotInserted                            = 20310,
    CMS_SIMPINNecessary                           = 20311,
    CMS_PHSIMPINNecessary                         = 20312,
    CMS_SIMFailure                                = 20313,
    CMS_SIMBusy                                   = 20314,
    CMS_SIMWrong                                  = 20315,
    CMS_SIMPUKRequired                            = 20316,
    CMS_SIMPIN2Required                           = 20317,
    CMS_SIMPUK2Required                           = 20318,
    CMS_MemoryFailure                             = 20320,
    CMS_InvalidMemoryIndex                        = 20321,
    CMS_MemoryFull                                = 20322,
    CMS_SMSCAddressUnknown                        = 20330,
    CMS_NoNetwork                                 = 20331,
    CMS_NetworkTimeout                            = 20332,
    CMS_Unknown                                   = 20500,
    CMS_SIMNotReady                               = 20512,
    CMS_MessageLengthExceeds                      = 20513,
    CMS_InvalidRequestParameters                  = 20514,
    CMS_MEStorageFailure                          = 20515,
    CMS_InvalidServiceMode                        = 20517,
    CMS_MoreMessageToSendStateError               = 20528,
    CMS_MOSMSIsNotAllow                           = 20529,
    CMS_GPRSIsSuspended                           = 20530,
    CMS_MEStorageFull                             = 20531,
    CMS_UnreadRecordsOnSIM                        = 23513,
    CMS_PSBusy                                    = 23515,
    CMS_CouldNotReadSMSParametersFromSIM          = 23516,
    CMS_SMNotReady                                = 23517,
    CMS_InvalidParameter                          = 23518,
    CMS_IncorrectOperFormat                       = 23742,
    CMS_InvalidInputValue                         = 23765,
    CMS_UnableToGetControlOfRequiredModule        = 23769,
    CMS_CallSetupInProgress                       = 23771,
    CMS_SIMPoweredDown                            = 23772,
    CMS_UnableToOperateInThisCFUNState            = 23773,
    CMS_InvalidARFCNInThisBand                    = 23774,
    CMS_ThePinIsNotInGPIOMode                     = 23775,
};

enum struct Encoding : uint8_t
{
    Unknown = 0xFF,

    UTF8     = 0,
    GSMBYTES = 1,
    UCS2LE   = 2,
    UCS2BE   = 3,
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

class BufferPrint : public Print
{
public:
    BufferPrint(uint8_t* buffer, size_t bufferSize)
        : bufferPtr(buffer)
        , bufferPtrEnd(buffer + bufferSize) {}
    
    BufferPrint(char* buffer, size_t bufferSize)
        : bufferPtr(reinterpret_cast<uint8_t*>(buffer))
        , bufferPtrEnd(reinterpret_cast<uint8_t*>(buffer + bufferSize - 1))
    {
        memset(buffer, '\0', bufferSize);
    }

    virtual size_t write(uint8_t octet) override
    {
        if (bufferPtr < bufferPtrEnd)
        {
            *bufferPtr++ = octet;
            return sizeof(uint8_t);
        }

        return 0;
    }

    virtual size_t write(const uint8_t* source, size_t sourceLength) override
    {
        size_t writeLength = bufferPtrEnd - bufferPtr;

        if (sourceLength < writeLength)
        {
            writeLength = sourceLength;
        }

        memcpy(bufferPtr, source, writeLength);
        bufferPtr += writeLength;

        return writeLength;
    }

private:
    uint8_t* bufferPtr;
    uint8_t* bufferPtrEnd;
};

bool skipCharacterInBuffer(const char*& characterPtr, char characterToSkip, bool caseSensitive = true)
{
    char characterToCompare = *characterPtr;

    if (!caseSensitive && (characterToSkip >= 'a' && characterToSkip <= 'z'))
    {
        characterToSkip    &= ~('A' ^ 'a');
        characterToCompare &= ~('A' ^ 'a');
    }

    if (characterToCompare == characterToSkip)
    {
        characterPtr++;
        return true;
    }

    return false;
}

template<typename char_predicate_t>
bool skipCharactersInBuffer(const char*& characterPtr, char_predicate_t isCharacterToSkip, size_t minCharactersToSkip, size_t maxCharactersToSkip)
{
    const char* characterToCheckPtr = characterPtr;
    const char* characterToCheckPtrEnd = characterPtr + maxCharactersToSkip;

    while (characterToCheckPtr < characterToCheckPtrEnd && isCharacterToSkip(*characterToCheckPtr))
    {
        characterToCheckPtr++;
    }

    if (characterToCheckPtr >= characterPtr + minCharactersToSkip)
    {
        characterPtr = characterToCheckPtr;
        return true;
    }

    return false;
}

bool skipCharactersInBuffer(const char*& characterPtr, const __FlashStringHelper* charactersToSkip, bool caseSensitive = true)
{
    const char* characterToCheckPtr = characterPtr;

    PGM_P characterToSkipPtr = reinterpret_cast<PGM_P>(charactersToSkip);

    while (true)
    {
        char characterToSkip = pgm_read_byte(characterToSkipPtr++);

        if (characterToSkip == '\0')
        {
            characterPtr = characterToCheckPtr;
            return true;
        }

        if (!skipCharacterInBuffer(characterToCheckPtr, characterToSkip, caseSensitive))
        {
            return false;
        }
    }
}

bool isHorizontalWhitespace(char character)
{
    return (character == '\t') ||
           (character == ' ');
}

bool skipHorizontalWhitespaceInBuffer(const char*& characterPtr, size_t minWhitespaceToSkip = 0, size_t maxWhitespaceToSkip = 255)
{
    return skipCharactersInBuffer(characterPtr, isHorizontalWhitespace, minWhitespaceToSkip, maxWhitespaceToSkip);
}

bool skipHorizontalWhitespaceInBuffer(const char*& characterPtr, const char* characterPtrEnd, size_t minWhitespaceToSkip = 0)
{
    size_t maxWhitespaceToSkip = characterPtrEnd - characterPtr;
    return skipHorizontalWhitespaceInBuffer(characterPtr, minWhitespaceToSkip, maxWhitespaceToSkip);
}

bool isEndOfBuffer(const char* characterPtr)
{
    return *characterPtr == '\0';
}

bool isNonWordCharacterInBuffer(const char* characterPtr)
{
    char character = *characterPtr;

    return (character != '_') &&
           (character < '0' || character > '9') &&
           (character < 'A' || character > 'Z') &&
           (character < 'a' || character > 'z');
}

constexpr uint16_t swapBytes(uint16_t value)
{
    return (value >> 8) | (value << 8);
}

unsigned long ModemTimeoutTime = 0;

void startModemTimeout(unsigned long timeout)
{
    ModemTimeoutTime = millis() + timeout;
}

bool isModemTimeout()
{
    signed long remainingMillis = static_cast<signed long>(ModemTimeoutTime - millis());
    return remainingMillis <= 0;
}

size_t ModemSubStreamDataRemaining = static_cast<size_t>(-1);

void startModemSubStream(size_t octetsInSubStream)
{
    ModemSubStreamDataRemaining = octetsInSubStream;
}

void closeModemSubStream()
{
    if (ModemSubStreamDataRemaining == static_cast<size_t>(-1))
    {
        Serial.println(F("SMSForwarder: Closed modem substream without having started one before"));
    }
    else if (ModemSubStreamDataRemaining != 0)
    {
        Serial.print(F("SMSForwarder: Closed modem substream without reading all of its data ("));
        Serial.print(ModemSubStreamDataRemaining);
        Serial.println(F(" octets left in substream)"));
    }

    ModemSubStreamDataRemaining = static_cast<size_t>(-1);
}

char ModemReadRollbackBuffer[16];
uint8_t ModemReadRollbackWriteOffset = sizeof(ModemReadRollbackBuffer) + 1;
uint8_t ModemReadRollbackReadOffset = 0;
uint8_t ModemReadRollbackReadLength = 0;

void markReadFromModemRollback()
{
    // If currently consuming rollback buffer, shift remaining contents to its start
    if (ModemReadRollbackReadOffset < ModemReadRollbackReadLength)
    {
        memmove(
            ModemReadRollbackBuffer,
            ModemReadRollbackBuffer + ModemReadRollbackReadOffset,
            ModemReadRollbackReadLength - ModemReadRollbackReadOffset);

        ModemReadRollbackReadLength -= ModemReadRollbackReadOffset;
        ModemReadRollbackReadOffset = 0;
    }
    else
    {
        ModemReadRollbackReadLength = 0;
        ModemReadRollbackReadOffset = 0;
    }

    // Collect characters read from the stream behind already buffered characters
    ModemReadRollbackWriteOffset = ModemReadRollbackReadLength;
}

void releaseReadFromModemRollback()
{
    // Disable collecting more into the buffer
    ModemReadRollbackWriteOffset = sizeof(ModemReadRollbackBuffer) + 1;
}

bool seekReadFromModemRollback()
{
    // If currently consuming rollback buffer, restart at its beginning
    if (ModemReadRollbackReadLength > 0)
    {
        if (ModemSubStreamDataRemaining != static_cast<size_t>(-1))
        {
            ModemSubStreamDataRemaining += ModemReadRollbackReadOffset;
        }

        ModemReadRollbackReadOffset = 0;
    }
    // Otherwise start consuming all buffered data
    else
    {
        if (ModemReadRollbackWriteOffset > sizeof(ModemReadRollbackBuffer))
        {
            return false;
        }

        ModemReadRollbackReadLength = ModemReadRollbackWriteOffset;
        ModemReadRollbackReadOffset = 0;

        if (ModemSubStreamDataRemaining != static_cast<size_t>(-1))
        {
            ModemSubStreamDataRemaining += ModemReadRollbackReadLength;
        }
    }

    // Disable collecting more into the buffer (needs a new mark)
    ModemReadRollbackWriteOffset = sizeof(ModemReadRollbackBuffer) + 1;

    return true;
}

void fakeReadFromModemRollback(const __FlashStringHelper* string)
{
    char* rollbackPtr = ModemReadRollbackBuffer;
    char* rollbackPtrEnd = ModemReadRollbackBuffer + sizeof(ModemReadRollbackBuffer);

    PGM_P stringPtr = reinterpret_cast<PGM_P>(string);

    while (rollbackPtr < rollbackPtrEnd)
    {
        char character = pgm_read_byte(stringPtr++);

        if (character == '\0')
        {
            break;
        }

        *rollbackPtr++ = character;
    }

    // Disable collecting more into the buffer
    ModemReadRollbackWriteOffset = sizeof(ModemReadRollbackBuffer) + 1;

    ModemReadRollbackReadLength = rollbackPtr - ModemReadRollbackBuffer;
    ModemReadRollbackReadOffset = 0;
}

SoftwareSerial ModemSerial(MODEM_SERIAL_RX_PIN, MODEM_SERIAL_TX_PIN);

int peekCharacterFromModem()
{
    if (ModemSubStreamDataRemaining == 0)
    {
        return ModemSerialState::EndOfStream;
    }

    if (ModemReadRollbackReadOffset < ModemReadRollbackReadLength)
    {
        return ModemReadRollbackBuffer[ModemReadRollbackReadOffset];
    }

    return ModemSerial.peek();
}

void discardCharacterFromModem()
{
    if (ModemSubStreamDataRemaining == 0)
    {
        return;
    }

    if (ModemReadRollbackReadOffset < ModemReadRollbackReadLength)
    {
        ModemReadRollbackReadOffset++;
        return;
    }

    int maybeCharacter = ModemSerial.read();

    if (maybeCharacter == ModemSerialState::NoData)
    {
        return;
    }

    if (ModemSubStreamDataRemaining != static_cast<size_t>(-1))
    {
        ModemSubStreamDataRemaining--;
    }

    if (ModemReadRollbackWriteOffset < sizeof(ModemReadRollbackBuffer))
    {
        char character = static_cast<char>(maybeCharacter);
        ModemReadRollbackBuffer[ModemReadRollbackWriteOffset] = character;
    }
    
    if (ModemReadRollbackWriteOffset <= sizeof(ModemReadRollbackBuffer))
    {
        ModemReadRollbackWriteOffset++;
    }
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

template<typename char_predicate_t>
bool skipCharacterFromModem(char_predicate_t isCharacterToSkip, bool echo = false)
{
    while (!isModemTimeout())
    {
        int maybeCharacter = peekCharacterFromModem();

        if (maybeCharacter == ModemSerialState::EndOfStream)
        {
            return false;
        }

        if (maybeCharacter != ModemSerialState::NoData)
        {
            if (!isCharacterToSkip(maybeCharacter))
            {
                break;
            }

            if (echo)
            {
                if (maybeCharacter >= 0x20)
                {
                    Serial.write(maybeCharacter);
                }
                else
                {
                    Serial.print(F("\\x"));
                    Serial.print(maybeCharacter / 0x10, HEX);
                    Serial.print(maybeCharacter & 0x0F, HEX);
                }
            }

            discardCharacterFromModem();
            return true;
        }
    }

    return false;
}

bool skipCharacterFromModem(char characterToSkip, bool echo = false)
{
    return skipCharacterFromModem([characterToSkip](char character) { return character == characterToSkip; }, echo);
}

bool skipCharactersFromModem(const __FlashStringHelper* charactersToSkip, bool skipTrailingHorizontalWhitespace = true)
{
    PGM_P characterToSkipPtr = reinterpret_cast<PGM_P>(charactersToSkip);

    while (true)
    {
        char characterToSkip = pgm_read_byte(characterToSkipPtr++);

        if (characterToSkip == '\0')
        {
            break;
        }

        if (!skipCharacterFromModem(characterToSkip))
        {
            return false;
        }
    }

    if (skipTrailingHorizontalWhitespace)
    {
        skipHorizontalWhitespaceFromModem();
    }

    return true;
}

template<typename char_predicate_t>
bool skipCharactersFromModemWhile(char_predicate_t isCharacterToSkip, bool echo = false)
{
    bool skipped = false;

    while (skipCharacterFromModem(isCharacterToSkip, echo))
    {
        skipped = true;
    }

    return skipped;
}

bool skipCharactersFromModemUntil(char stopCharacter, bool echo = false)
{
    return skipCharactersFromModemWhile([stopCharacter](char character) { return character != stopCharacter; }, echo);
}

template<typename char_predicate_t>
bool readCharacterFromModem(char& character, char_predicate_t isCharacterToRead)
{
    while (!isModemTimeout())
    {
        int maybeCharacter = peekCharacterFromModem();

        if (maybeCharacter == ModemSerialState::EndOfStream)
        {
            return false;
        }

        if (maybeCharacter != ModemSerialState::NoData)
        {
            if (!isCharacterToRead(maybeCharacter))
            {
                break;
            }

            discardCharacterFromModem();
            character = static_cast<char>(maybeCharacter);
            return true;
        }
    }

    return false;
}

template<typename char_predicate_t>
size_t readCharactersFromModemWhile(char* buffer, size_t bufferSize, char_predicate_t isCharacterToRead)
{
    char* characterPtr = buffer;
    char* characterPtrEnd = buffer + bufferSize - 1;

    while (characterPtr < characterPtrEnd)
    {
        if (readCharacterFromModem(*characterPtr, isCharacterToRead))
        {
            characterPtr++;
            continue;
        }

        break;
    }

    *characterPtr = '\0';

    return static_cast<size_t>(characterPtr - buffer);
}

size_t readCharactersFromModemUntil(char* buffer, size_t bufferSize, char stopCharacter)
{
    return readCharactersFromModemWhile(buffer, bufferSize, [stopCharacter](char character) { return character != stopCharacter; });
}

size_t readLineFromModem(char* buffer, size_t bufferSize, bool skipEndOfLine = true)
{
    size_t bufferLength = readCharactersFromModemUntil(buffer, bufferSize, '\r');

    if (skipEndOfLine)
    {
        skipEndOfLineFromModem();
    }

    return bufferLength;
}

bool isDecDigit(char character)
{
    return (character >= '0' && character <= '9');
}

size_t skipDigitsFromModem()
{
    return skipCharactersFromModemWhile(isDecDigit);
}

size_t readDigitsFromModem(char* buffer, size_t bufferSize)
{
    return readCharactersFromModemWhile(buffer, bufferSize, isDecDigit);
}

void skipHorizontalWhitespaceFromModem()
{
    skipCharactersFromModemWhile(isHorizontalWhitespace);
}

bool skipEndOfLineFromModem()
{
    bool skipped;

    skipped = skipCharacterFromModem('\r');
    skipped = skipCharacterFromModem('\n') || skipped;

    return skipped;
}

void discardLineFromModem(bool echo = false)
{
    skipCharactersFromModemUntil('\r', echo);

    if (echo)
    {
        Serial.println();
    }

    skipEndOfLineFromModem();
}

uint8_t ModemCommandBuffer[180*3] = {0};

size_t computeAllocatedModemCommandSize()
{
    uint8_t* modemCommandEntryPtr = ModemCommandBuffer;
    uint8_t* modemCommandEntryPtrEnd = ModemCommandBuffer + sizeof(ModemCommandBuffer) - sizeof(size_t) + 1;

    while (modemCommandEntryPtr < modemCommandEntryPtrEnd)
    {
        size_t skipModemCommandSize = *reinterpret_cast<size_t*>(modemCommandEntryPtr);
        
        if (skipModemCommandSize == 0)
        {
            break;
        }

        modemCommandEntryPtr += sizeof(size_t);
        modemCommandEntryPtr += skipModemCommandSize;
    }

    return modemCommandEntryPtr - ModemCommandBuffer;
}

void* allocateModemCommand(size_t modemCommandSize)
{
    size_t allocatedModemCommandEntrySize = computeAllocatedModemCommandSize();

    uint8_t* modemCommandEntryPtr = ModemCommandBuffer + allocatedModemCommandEntrySize;

    allocatedModemCommandEntrySize += sizeof(size_t);
    allocatedModemCommandEntrySize += modemCommandSize;

    if (allocatedModemCommandEntrySize > sizeof(ModemCommandBuffer))
    {
        return nullptr;
    }

    *reinterpret_cast<size_t*>(modemCommandEntryPtr) = modemCommandSize;
    
    if (allocatedModemCommandEntrySize + sizeof(size_t) <= sizeof(ModemCommandBuffer))
    {
        *reinterpret_cast<size_t*>(ModemCommandBuffer + allocatedModemCommandEntrySize) = 0;
    }

    return modemCommandEntryPtr + sizeof(size_t);
}

template<typename modem_command_t, typename... args_t>
modem_command_t* enqueueModemCommand(args_t&&... args)
{
    void* modemCommandPtr = allocateModemCommand(sizeof(modem_command_t));

    if (modemCommandPtr == nullptr)
    {
        return nullptr;
    }

    return new(modemCommandPtr) modem_command_t(std::forward<args_t>(args)...);
}

ModemCommand* peekModemCommand()
{
    size_t modemCommandSize = *reinterpret_cast<size_t*>(ModemCommandBuffer);

    if (modemCommandSize == 0)
    {
        return nullptr;
    }

    return reinterpret_cast<ModemCommand*>(ModemCommandBuffer + sizeof(size_t));
}

void dequeueModemCommand()
{
    size_t modemCommandSize = *reinterpret_cast<size_t*>(ModemCommandBuffer);

    if (modemCommandSize == 0)
    {
        return;
    }

    ModemCommand* modemCommand = reinterpret_cast<ModemCommand*>(ModemCommandBuffer + sizeof(size_t));
    modemCommand->~ModemCommand();

    size_t remainingModemCommandEntrySize = computeAllocatedModemCommandSize();
    remainingModemCommandEntrySize -= sizeof(size_t);
    remainingModemCommandEntrySize -= modemCommandSize;

    if (remainingModemCommandEntrySize == 0)
    {
        *reinterpret_cast<size_t*>(ModemCommandBuffer) = 0;
    }
    else
    {
        memmove(
            ModemCommandBuffer,
            ModemCommandBuffer + sizeof(size_t) + modemCommandSize,
            remainingModemCommandEntrySize);

        if (remainingModemCommandEntrySize + sizeof(size_t) <= sizeof(ModemCommandBuffer))
        {
            *reinterpret_cast<size_t*>(ModemCommandBuffer + remainingModemCommandEntrySize) = 0;
        }
    }
}

void cancelModemCommand(ModemCommand* modemCommand)
{
    if (modemCommand == nullptr)
    {
        return;
    }

    uint8_t* modemCommandEntryPtr = reinterpret_cast<uint8_t*>(modemCommand) - sizeof(size_t);

    size_t allocatedModemCommandEntrySize = computeAllocatedModemCommandSize();

    if (modemCommandEntryPtr < ModemCommandBuffer ||
        modemCommandEntryPtr >= ModemCommandBuffer + allocatedModemCommandEntrySize)
    {
        return;
    }

    size_t modemCommandSize = *reinterpret_cast<size_t*>(modemCommandEntryPtr);

    modemCommand->~ModemCommand();

    size_t prevModemCommandEntrySize = modemCommandEntryPtr - ModemCommandBuffer;
    size_t nextModemCommandEntrySize = allocatedModemCommandEntrySize - prevModemCommandEntrySize - (sizeof(size_t) + modemCommandSize);

    if (nextModemCommandEntrySize == 0)
    {
        *reinterpret_cast<size_t*>(modemCommandEntryPtr) = 0;
    }
    else
    {
        memmove(
            ModemCommandBuffer + prevModemCommandEntrySize,
            ModemCommandBuffer + prevModemCommandEntrySize + sizeof(size_t) + modemCommandSize,
            nextModemCommandEntrySize);

        if (prevModemCommandEntrySize + nextModemCommandEntrySize + sizeof(size_t) < sizeof(ModemCommandBuffer))
        {
            *reinterpret_cast<size_t*>(ModemCommandBuffer + prevModemCommandEntrySize + nextModemCommandEntrySize) = 0;
        }
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

void printCharacter(Stream& stream, char character, bool escapeSpecialCharacters)
{
    if (escapeSpecialCharacters)
    {
        switch (character)
        {
            /*
            case '\0': stream.print(F("<NUL>")); return;
            case '\t': stream.print(F("<TAB>")); return;
            case '\r': stream.print(F("<CR>")); return;
            case '\n': stream.print(F("<LF>")); return;
            /*/
            case '\0': stream.print(F("\xE2\x90\x80")); return; // U+2400 SYMBOL FOR NULL
            case '\t': stream.print(F("\xE2\x87\xA5")); return; // U+21E5 RIGHTWARDS ARROW TO BAR
            case '\r': stream.print(F("\xE2\x87\xA4")); return; // U+21E4 LEFTWARDS ARROW TO BAR
            case '\n': stream.print(F("\xE2\x86\xB5")); return; // U+21B5 DOWNWARDS ARROW WITH CORNER LEFTWARDS
            //*
        }
    }

    stream.print(character);
}

void printUnicode(Stream& stream, wchar_t codepoint, bool escapeSpecialCharacters = false)
{
    if (codepoint <= 0x7F)
    {
        printCharacter(stream, static_cast<char>(codepoint), escapeSpecialCharacters);
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

void printEncoded(Stream& stream, const char* buffer, size_t bufferSize, Encoding encoding = Encoding::UTF8, bool escapeSpecialCharacters = false)
{
    switch (encoding)
    {
        case Encoding::UTF8:
        {
            const char* characterPtr = buffer;
            const char* characterPtrEnd = buffer + bufferSize;

            for (; characterPtr < characterPtrEnd; ++characterPtr)
            {
                char character = *characterPtr;

                if (character == '\0')
                {
                    break;
                }

                printCharacter(stream, character, escapeSpecialCharacters);
            }
        }
        return;

        case Encoding::UCS2LE:
        {
            const wchar_t* bufferCodepointPtr = reinterpret_cast<const wchar_t*>(buffer);
            const wchar_t* bufferCodepointPtrEnd = reinterpret_cast<const wchar_t*>(buffer + (bufferSize & ~1));

            for (; bufferCodepointPtr < bufferCodepointPtrEnd; ++bufferCodepointPtr)
            {
                wchar_t codepoint = *bufferCodepointPtr;

                if (codepoint == '\0')
                {
                    break;
                }

                printUnicode(stream, codepoint, escapeSpecialCharacters);
            }
        }
        break;

        case Encoding::UCS2BE:
        {
            const wchar_t* bufferCodepointPtr = reinterpret_cast<const wchar_t*>(buffer);
            const wchar_t* bufferCodepointPtrEnd = reinterpret_cast<const wchar_t*>(buffer + (bufferSize & ~1));

            for (; bufferCodepointPtr < bufferCodepointPtrEnd; ++bufferCodepointPtr)
            {
                wchar_t codepoint = swapBytes(*bufferCodepointPtr);

                if (codepoint == '\0')
                {
                    break;
                }

                printUnicode(stream, codepoint, escapeSpecialCharacters);
            }
        }
        break;

        case Encoding::GSMBYTES:
        {
            const char* characterPtr = buffer;
            const char* characterPtrEnd = buffer + bufferSize;

            bool isExtended = false;

            for (; characterPtr < characterPtrEnd; ++characterPtr)
            {
                unsigned char character = *characterPtr;

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
                printUnicode(stream, codepoint, escapeSpecialCharacters);

                isExtended = false;
            }
        }
        break;

        default: break;
    }
}

void printlnEncoded(Stream& stream, const char* buffer, size_t bufferSize, Encoding encoding = Encoding::UTF8, bool escapeSpecialCharacters = false)
{
    printEncoded(stream, buffer, bufferSize, encoding, escapeSpecialCharacters);
    stream.write('\n');
}

bool isHexDigit(char character)
{
    return (character >= '0' && character <= '9') ||
           (character >= 'A' && character <= 'F') ||
           (character >= 'a' && character <= 'f');
}

uint8_t parseHexFromModem(uint8_t resultOnError = 0xFF)
{
    uint8_t result = 0;

    for (uint8_t numHexDigits = 2; numHexDigits > 0; --numHexDigits)
    {
        char hexDigit;

        if (readCharacterFromModem(hexDigit, isHexDigit))
        {
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
    for (; numHexDigits > 0; --numHexDigits)
    {
        if (!skipCharacterFromModem(isHexDigit))
        {
            return false;
        }
    }

    return true;
}

void sendHexToModem(uint8_t value)
{
    for (uint8_t numHexDigits = 2; numHexDigits > 0; --numHexDigits)
    {
        char hexDigit;
        
        hexDigit = value >> 4;
        hexDigit += (hexDigit < 10) ? '0' : ('A' - 10);

        ModemSerial.write(hexDigit);

        value <<= 4;
    }
}

template<typename command_char_t>
void sendCommandToModem(const command_char_t* command, unsigned long responseTimeout)
{
    ModemSerial.print(command);
    ModemSerial.print('\r');

    startModemTimeout(responseTimeout);
}

template<typename command_char_t, typename arguments_t>
void sendCommandToModem(const command_char_t* command, const arguments_t arguments, unsigned long responseTimeout)
{
    ModemSerial.print(command);
    ModemSerial.print(arguments);
    ModemSerial.print('\r');

    startModemTimeout(responseTimeout);
}

template<typename command_char_t, typename argument1_t, typename argument2_t>
void sendCommandToModem(const command_char_t* command, const argument1_t argument1, const argument2_t argument2, unsigned long responseTimeout)
{
    ModemSerial.print(command);
    ModemSerial.print(argument1);
    ModemSerial.print(argument2);
    ModemSerial.print('\r');

    startModemTimeout(responseTimeout);
}

ModemResult parseResultFromModem()
{
    char resultCode[4+1];

    markReadFromModemRollback();

    if (readDigitsFromModem(resultCode, sizeof(resultCode)) &&
        skipCharacterFromModem('\r'))
    {
        releaseReadFromModemRollback();
        return static_cast<ModemResult>(atoi(resultCode));
    }

    if (skipCharactersFromModem(F("+CME ERROR:")) &&
        readDigitsFromModem(resultCode, sizeof(resultCode)) &&
        skipCharacterFromModem('\r'))
    {
        releaseReadFromModemRollback();
        return static_cast<ModemResult>(atoi(resultCode) + static_cast<int>(ModemResult::CME));
    }

    if (skipCharactersFromModem(F("+CMS ERROR:")) &&
        readDigitsFromModem(resultCode, sizeof(resultCode)) &&
        skipCharacterFromModem('\r'))
    {
        releaseReadFromModemRollback();
        return static_cast<ModemResult>(atoi(resultCode) + static_cast<int>(ModemResult::CMS));
    }

    seekReadFromModemRollback();
    return ModemResult::None;
}

ModemResult discardResponseFromModem()
{
    for (uint8_t numResponseLines = 2; numResponseLines != 0; --numResponseLines)
    {
        ModemResult modemResult = parseResultFromModem();

        if (modemResult != ModemResult::None)
        {
            return modemResult;
        }

        discardLineFromModem();
    }

    return ModemResult::None;
}

bool queryIMEI()
{
    char IMEI[15+1];

    sendCommandToModem(F("AT+GSN"), 300);

    if (readDigitsFromModem(IMEI, sizeof(IMEI)) &&
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
    Serial.print(IMEI);
    Serial.println();
    return true;
}

bool enterSIMPIN(const char* pin)
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

    if (pin == nullptr || pin[0] == '\0')
    {
        Serial.println(F("GSM: PIN required but not configured"));
        return false;
    }
    
    Serial.println(F("GSM: Entering PIN"));

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

void sendQueryCallForwardingCommand()
{
    // Query call forwarding
    //   <reads>=0: unconditional forwarding
    //   <mode>=2: query
    //   <number>
    //   <type>
    //   <class>=1: voice calls
    sendCommandToModem(F("AT+CCFC=0,2,,,1"), 10000);
}

bool parseQueryCallForwardingResponse(char* destinationBuffer, size_t destinationBufferSize)
{
    destinationBuffer[0] = '\0';

    bool gotResponse = false;
    
    while (true)
    {
        markReadFromModemRollback();

        char forwardingStatus[1+1];
        char forwardingClass[2+1];

        if (skipCharactersFromModem(F("+CCFC:")) &&
            readDigitsFromModem(forwardingStatus, sizeof(forwardingStatus)) &&
            skipCharacterFromModem(',') &&
            readDigitsFromModem(forwardingClass, sizeof(forwardingClass)))
        {
            releaseReadFromModemRollback();

            // Read call forwarding destination if enabled for voice calls
            if (forwardingStatus[0] == '1' &&
                forwardingClass[0] == '1' &&
                skipCharacterFromModem(',') &&
                skipCharacterFromModem('"') &&
                readCharactersFromModemUntil(destinationBuffer, destinationBufferSize, '"') &&
                skipCharacterFromModem('"'))
            {
                // Parsed call forwarding destination successfully
            }

            gotResponse = true;

            // Skip remaining fields including end-of-line
            discardLineFromModem();
        }
        else
        {
            // Roll back to start of line
            seekReadFromModemRollback();

            break;
        }
    }

    return gotResponse;
}

void queryCallForwardingAsync(void (*callback)(ModemResult modemResult, const char* destination))
{
    struct QueryCallForwardingModemCommand : ModemCommand
    {
        // Callback after the call forwarding disable request has completed
        // - Can be nullptr if no callback is needed
        // - Executed both on success or failure, indicated through modemResult
        // - When called on failure, destination is nullptr
        void (*callback)(ModemResult modemResult, const char* destination);

        virtual bool sendCommand() override
        {
            sendQueryCallForwardingCommand();

            return true;
        }

        virtual bool tryProcessResponse() override
        {
            char destination[17+1];

            if (parseQueryCallForwardingResponse(destination, sizeof(destination)))
            {
                // Parsed call forwarding destination successfully
            }
            else
            {
                // Not the expected response
                return false;
            }

            ModemResult modemResult = parseResultFromModem();

            if (modemResult == ModemResult::None)
            {
                return false;
            }

            if (callback != nullptr)
            {
                bool hasDestination = destination[0] != '\0';
                callback(modemResult, hasDestination ? destination : nullptr);
            }

            return true;
        }
    };

    QueryCallForwardingModemCommand* modemCommand = enqueueModemCommand<QueryCallForwardingModemCommand>();

    if (modemCommand == nullptr)
    {
        Serial.println(F("SMSForwarder: Cannot query voice call forwarding (modem command queue full)"));
        return;
    }

    modemCommand->callback = callback;
    modemCommand->state = ModemCommandState::ReadyToSendCommand;
}

void sendDisableCallForwardingCommand()
{
    // Disable call forwarding
    //   <reads>=0: unconditional forwarding
    //   <mode>=4: erasure
    //   <number>
    //   <type>
    //   <class>=1: voice calls
    sendCommandToModem(F("AT+CCFC=0,4,,,1"), 10000);
}

void disableCallForwardingAsync(void (*callback)(ModemResult modemResult))
{
    struct DisableCallForwardingModemCommand : ModemCommand
    {
        // Callback after the call forwarding disable request has completed
        // - Can be nullptr if no callback is needed
        // - Executed both on success or failure, indicated through modemResult
        void (*callback)(ModemResult modemResult);

        virtual bool sendCommand() override
        {
            sendDisableCallForwardingCommand();

            return true;
        }

        virtual bool tryProcessResponse() override
        {
            ModemResult modemResult = parseResultFromModem();

            if (modemResult == ModemResult::None)
            {
                return false;
            }

            if (callback != nullptr)
            {
                callback(modemResult);
            }

            return true;
        }
    };

    DisableCallForwardingModemCommand* modemCommand = enqueueModemCommand<DisableCallForwardingModemCommand>();

    if (modemCommand == nullptr)
    {
        Serial.println(F("SMSForwarder: Cannot disable voice call forwarding (modem command queue full)"));
        return;
    }

    modemCommand->callback = callback;
    modemCommand->state = ModemCommandState::ReadyToSendCommand;
}

void sendUpdateCallForwardingCommand(const char* destination)
{
    // Enable call forwarding
    //   <reads>=0: unconditional forwarding
    //   <mode>=3: registration
    //   <number>: forwarding number
    //   <type>=145: international number
    //   <class>=1: voice calls
    sendCommandToModem(F("AT+CCFC=0,3,"), destination, F(",145,1"), 10000);
}

void updateCallForwardingAsync(const char* destination, void (*callback)(ModemResult modemResult, const char* destination))
{
    struct UpdateCallForwardingModemCommand : ModemCommand
    {
        // Callback after the call forwarding update request has completed
        // - Can be nullptr if no callback is needed
        // - Executed both on success or failure, indicated through modemResult
        // - When called on failure, destination is nullptr
        void (*callback)(ModemResult modemResult, const char* destination);

        // Requested updated call forwarding number
        char destination[17+1];

        virtual bool sendCommand() override
        {
            sendUpdateCallForwardingCommand(destination);

            return true;
        }

        virtual bool tryProcessResponse() override
        {
            ModemResult modemResult = parseResultFromModem();

            if (modemResult == ModemResult::None)
            {
                return false;
            }

            if (callback != nullptr)
            {
                callback(modemResult, modemResult == ModemResult::OK ? destination : nullptr);
            }

            return true;
        }
    };

    UpdateCallForwardingModemCommand* modemCommand = enqueueModemCommand<UpdateCallForwardingModemCommand>();

    if (modemCommand == nullptr)
    {
        Serial.println(F("SMSForwarder: Cannot update voice forwarding (modem command queue full)"));
        return;
    }

    size_t destinationLength = strlen(destination);

    if (destinationLength >= sizeof(modemCommand->destination))
    {
        cancelModemCommand(modemCommand);

        Serial.println(F("SMSForwarder: Cannot update voice forwarding (number too long)"));
        return;
    }

    strcpy(modemCommand->destination, destination);

    modemCommand->callback = callback;
    modemCommand->state = ModemCommandState::ReadyToSendCommand;
}

uint8_t parseGSMAlphaFromModem(char* characterBuffer, size_t characterBufferSize, uint8_t charactersToParse)
{
    uint8_t characterPosition = 0;

    char character = '\0';
    char characterRemainder = '\0';

    char* characterPtr = characterBuffer;
    char* characterPtrEnd = characterBuffer + characterBufferSize - 1;

    for (; charactersToParse > 0; --charactersToParse)
    {
        uint8_t octet = parseHexFromModem();

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

        if (characterPtr < characterPtrEnd)
        {
            *characterPtr++ = character;
        }

        if (characterPosition == 7)
        {
            if (charactersToParse == 1)
            {
                break;
            }

            if (characterPtr < characterPtrEnd)
            {
                *characterPtr++ = characterRemainder;
            }

            characterPosition = 0;
            charactersToParse--;
        }
    }

    *characterPtr = '\xFF';

    return isModemTimeout() ? 0 : static_cast<uint8_t>(characterPtr - characterBuffer);
}

void sendGSMAlphaToModem(const char* characters, uint8_t charactersToWrite)
{
    uint8_t characterPosition = 0;

    uint8_t octet = 0x00;

    const char* characterPtr = characters;
    const char* characterPtrEnd = characters + charactersToWrite;

    while (characterPtr < characterPtrEnd)
    {
        unsigned char character = *characterPtr++ & 0x7F;

        switch (characterPosition++)
        {
            case 0:
                octet = 0x80 | character;
                break;

            case 1:
                octet = (octet & 0x7F) | (character & 0x01) << 7;
                sendHexToModem(octet);
                octet = 0xC0 | (character >> 1);
                break;

            case 2:
                octet = (octet & 0x3F) | (character & 0x03) << 6;
                sendHexToModem(octet);
                octet = 0xE0 | (character >> 2);
                break;

            case 3:
                octet = (octet & 0x1F) | (character & 0x07) << 5;
                sendHexToModem(octet);
                octet = 0xF0 | (character >> 3);
                break;

            case 4:
                octet = (octet & 0x0F) | (character & 0x0F) << 4;
                sendHexToModem(octet);
                octet = 0xF8 | (character >> 4);
                break;

            case 5:
                octet = (octet & 0x07) | (character & 0x1F) << 3;
                sendHexToModem(octet);
                octet = 0xFC | (character >> 5);
                break;

            case 6:
                octet = (octet & 0x03) | (character & 0x3F) << 2;
                sendHexToModem(octet);
                octet = 0xFE | (character >> 6);
                break;

            case 7:
                octet = (octet & 0x01) | character << 1;
                sendHexToModem(octet);
                characterPosition = 0;
                break;
        }
    }

    if (characterPosition != 0)
    {
        sendHexToModem(octet);
    }
}

bool parseSMSPDUHeaderFromModem(char* senderBuffer, size_t senderBufferSize, uint8_t& senderLengthOut, bool& hasUserDataHeaderOut)
{
    senderBuffer[0] = '\xFF';
    senderLengthOut = 0;

    // Skip SMSC address:
    //   byte  0:    number of octets
    //   bytes 1..n: SMS message center address
    {
        uint8_t dispatcherAddressLength = parseHexFromModem();

        if (dispatcherAddressLength > 12 || !skipHexFromModem(2 * dispatcherAddressLength))
        {
            return false;
        }
    }

    hasUserDataHeaderOut = false;

    // Read message header:
    //   bits 0..1: TP-MTI  (message type indicator)
    //   bit  2:    TP-MMS  (more messages to send)
    //   bit  5:    TP-SRI  (status report indication)
    //   bit  6:    TP-UDHI (user data header indicator)
    //   bit  7:    TP-RP   (reply path)
    {
        uint8_t header = parseHexFromModem();

        const uint8_t TP_MTI_MASK        = 0x03;
        const uint8_t TP_MTI_SMS_DELIVER = 0x00;

        const uint8_t TP_UDHI_MASK       = 0x40;
        const uint8_t TP_UDHI_PRESENT    = 0x40;

        if (header == 0xFF || (header & TP_MTI_MASK) != TP_MTI_SMS_DELIVER)
        {
            return false;
        }

        hasUserDataHeaderOut = (header & TP_UDHI_MASK) == TP_UDHI_PRESENT;
    }

    // Read TP-OA (originating address):
    //   byte  0:            number of useful semi-octets within the value field
    //   byte  1, bit  7:    type of address header fixed bit
    //   byte  1, bits 4..6: type of number
    //   byte  1, bits 0..3: numbering plan identification
    //   bytes 2..n:         originating address encoded as per type of number
    {
        uint8_t senderAddressSemiOctets = parseHexFromModem();
        uint8_t senderAddressType = parseHexFromModem();

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
                char* senderCharacterPtr = senderBuffer;
                char* senderCharacterPtrEnd = senderBuffer + senderBufferSize - 1;

                if (senderCharacterPtr < senderCharacterPtrEnd)
                {
                    *senderCharacterPtr++ = '+';
                }

                for (uint8_t senderAddressOctets = (senderAddressSemiOctets + 1) >> 1; senderAddressOctets > 0; --senderAddressOctets)
                {
                    uint8_t senderAddressOctet = parseHexFromModem();

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

            case ADDRESS_TYPE_NUMBER_NATIONAL:
            case ADDRESS_TYPE_NUMBER_NETWORKSPECIFIC:
            case ADDRESS_TYPE_NUMBER_SUBSCRIBER:
            case ADDRESS_TYPE_NUMBER_ABBREVIATED:
            {
                char* senderCharacterPtr = senderBuffer;
                char* senderCharacterPtrEnd = senderBuffer + senderBufferSize - 1;

                for (uint8_t senderAddressOctets = (senderAddressSemiOctets + 1) >> 1; senderAddressOctets > 0; --senderAddressOctets)
                {
                    uint8_t senderAddressOctet = parseHexFromModem();

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

    return !isModemTimeout();
}

bool parseSMSPDUBodyFromModem(bool hasUserDataHeader, char* messageBuffer, size_t messageBufferSize, Encoding& messageEncodingOut, uint8_t& messageLengthOut)
{
    messageBuffer[0] = '\xFF';
    messageEncodingOut = Encoding::Unknown;
    messageLengthOut = 0;

    uint8_t messageCoding;

    const uint8_t TP_DCS_GSM7BIT = 0x00;
    const uint8_t TP_DCS_UCS2BE  = 0x08;

    // Read TP-DCS (data coding scheme)
    {
        messageCoding = parseHexFromModem();

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

    // Read message header:
    //   byte  0:    TP-UDL (user data length)
    //   bytes 1..n: TP-UD  (user data)
    {
        uint8_t messageLength = parseHexFromModem();

        if (messageLength > 160)
        {
            return false;
        }

        if (hasUserDataHeader)
        {
            uint8_t userDataHeaderLength = parseHexFromModem();

            if (userDataHeaderLength + 1 > messageLength)
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

            messageLength -= userDataHeaderLength + 1;
        }

        switch (messageCoding)
        {
            case TP_DCS_GSM7BIT:
            {
                messageEncodingOut = Encoding::GSMBYTES;
                messageLengthOut = parseGSMAlphaFromModem(messageBuffer, messageBufferSize, messageLength);

                if (messageLengthOut == 0)
                {
                    return false;
                }
            }
            break;

            case TP_DCS_UCS2BE:
            {
                messageEncodingOut = Encoding::UCS2BE;

                char* messageBytePtr = messageBuffer;
                char* messageBytePtrEnd = messageBuffer + messageBufferSize - sizeof(wchar_t);

                for (; messageLength > 0; --messageLength)
                {
                    uint8_t messageByte = parseHexFromModem();

                    if (messageBytePtr < messageBytePtrEnd)
                    {
                        *messageBytePtr++ = messageByte;
                    }
                }

                *reinterpret_cast<wchar_t*>(messageBytePtr) = '\0';

                messageLengthOut = static_cast<uint8_t>(messageBytePtr - messageBuffer);
            }
            break;
        }
    }

    return !isModemTimeout();
}

bool parseSMSPDUFromModem(char* senderBuffer, size_t senderBufferSize, uint8_t& senderLengthOut, char* messageBuffer, size_t messageBufferSize, Encoding& messageEncodingOut, uint8_t& messageLengthOut)
{
    bool hasUserDataHeader;
    
    return parseSMSPDUHeaderFromModem(senderBuffer, senderBufferSize, senderLengthOut, hasUserDataHeader) &&
           parseSMSPDUBodyFromModem(hasUserDataHeader, messageBuffer, messageBufferSize, messageEncodingOut, messageLengthOut);
}

size_t computeSMSPDUSize(const char* destination, size_t destinationLength, Encoding messageEncoding, size_t messageLength)
{
    size_t size = 0;

    // SMSC address is not included

    size += 1;  // TP-MTI, TP-RD, TP-VPF, TP-SRR, TP-UDHI, TP-RP
    size += 1;  // TP-MR
    size += 1;  // TP-DA address length
    size += 1;  // TP-DA type of address

    if (destinationLength == 0)
    {
        // Empty destination is invalid
        return 0;
    }

    const char* destinationCharacterPtr = destination;
    const char* destinationCharacterPtrEnd = destination + destinationLength;

    if (*destinationCharacterPtr == '+')
    {
        if (destinationLength < 8)
        {
            // Destination too short for an international number
            return 0;
        }

        // Destination without leading '+', two digits per octet, rounded up
        size += destinationLength >> 1;

        destinationCharacterPtr++;
    }
    else
    {
        // Destination number as is, two digits per octet, rounded up
        size += (destinationLength + 1) >> 1;
    }

    while (destinationCharacterPtr < destinationCharacterPtrEnd)
    {
        char destinationCharacter = *destinationCharacterPtr++;

        if (destinationCharacter < '0' ||
            destinationCharacter > '9')
        {
            // Invalid non-digit character in destination
            return 0;
        }
    }

    size += 1;  // TP-PID
    size += 1;  // TP-DCS
    size += 0;  // TP-VP [not present]
    size += 1;  // TP-UDL

    switch (messageEncoding)
    {
        case Encoding::GSMBYTES:
            // Message as 7 bits per input message byte, rounded up
            size += ((messageLength + 1) * 7) / 8;
            break;

        case Encoding::UCS2BE:
            // Message as one 16 bit word per two input message bytes
            size += messageLength;
            break;

        default:
            // Unsupported message encoding
            return 0;
    }

    return size;
}

bool sendSMSPDUToModem(const char* destination, size_t destinationLength, const char* message, Encoding messageEncoding, uint8_t messageLength)
{
    // Send SMSC address:
    //   byte 0: number of octets
    {
        sendHexToModem(0);
    }

    // Send message header:
    //   bits 0..1: TP_MTI  (message type indicator)
    //   bit  2:    TP_RD   (reject duplicates)
    //   bits 3..4: TP_VPF  (validity period format)
    //   bit  5:    TP_SRR  (status report request)
    //   bit  6:    TP_UDHI (user data header indicator)
    //   bit  7:    TP_RP   (reply path)
    {
        const uint8_t TP_MTI_SMS_SUBMIT    = 0x01;  // bits 0..1
        const uint8_t TP_RD_NOT_REQUESTED  = 0x00;  // bit  2
        const uint8_t TP_VPF_NOT_PRESENT   = 0x00;  // bits 3..4
        const uint8_t TP_SRR_NOT_REQUESTED = 0x00;  // bit  5
        const uint8_t TP_UDHI_NOT_PRESENT  = 0x00;  // bit  6
        const uint8_t TP_RP_NOT_SET        = 0x00;  // bit  7

        sendHexToModem(
            TP_MTI_SMS_SUBMIT |
            TP_RD_NOT_REQUESTED |
            TP_VPF_NOT_PRESENT |
            TP_SRR_NOT_REQUESTED |
            TP_UDHI_NOT_PRESENT |
            TP_RP_NOT_SET);
    }

    // Send TP-MR (message reference)
    {
        // Automatically substituted by the modem
        sendHexToModem(0);
    }

    // Send TP-DA (destination address):
    //   byte  0:            number of useful semi-octets within the value field
    //   byte  1, bit  7:    type of address header fixed bit
    //   byte  1, bits 4..6: type of number
    //   byte  1, bits 0..3: numbering plan identification
    //   bytes 2..n:         originating address encoded as per type of number
    {
        if (destinationLength == 0)
        {
            // Cancel sending
            ModemSerial.write(0x1B);
            return false;
        }

        const uint8_t ADDRESS_FIXEDBIT                  = 0x80;
        const uint8_t ADDRESS_TYPE_NUMBER_INTERNATIONAL = 0x10;
        const uint8_t ADDRESS_TYPE_NUMBER_NATIONAL      = 0x20;
        const uint8_t ADDRESS_NUMBERING_PLAN_TELEPHONE  = 0x01;

        const char* destinationCharacterPtr = destination;
        const char* destinationCharacterPtrEnd = destination + destinationLength;

        if (*destinationCharacterPtr == '+')
        {
            sendHexToModem(destinationLength - 1);

            sendHexToModem(
                ADDRESS_FIXEDBIT |
                ADDRESS_TYPE_NUMBER_INTERNATIONAL |
                ADDRESS_NUMBERING_PLAN_TELEPHONE);
            
            destinationCharacterPtr++;
        }
        else
        {
            sendHexToModem(destinationLength);

            sendHexToModem(
                ADDRESS_FIXEDBIT |
                ADDRESS_TYPE_NUMBER_NATIONAL |
                ADDRESS_NUMBERING_PLAN_TELEPHONE);
        }

        while (destinationCharacterPtr < destinationCharacterPtrEnd)
        {
            uint8_t destinationAddressOctet =
                (*destinationCharacterPtr++ - '0');

            destinationAddressOctet |=
                destinationCharacterPtr < destinationCharacterPtrEnd
                    ? (*destinationCharacterPtr++ - '0') << 4 : 0xF0;

            sendHexToModem(destinationAddressOctet);
        }
    }

    // Send TP-PID (protocol identifier):
    //   bits 6..7: type
    //   bit  5:    telematic interworking
    //   bits 0..4: telematic device
    {
        const uint8_t TP_PID_TYPE_STANDARD   = 0x00;  // bits 6..7
        const uint8_t TP_PID_NO_INTERWORKING = 0x00;  // bit  5

        sendHexToModem(
            TP_PID_TYPE_STANDARD |
            TP_PID_NO_INTERWORKING);
    }

    uint8_t messageCoding;

    const uint8_t TP_DCS_GSM7BIT = 0x00;
    const uint8_t TP_DCS_UCS2BE  = 0x08;

    // Send TP-DCS (data coding scheme)
    {
        switch (messageEncoding)
        {
            case Encoding::GSMBYTES:
                messageCoding = TP_DCS_GSM7BIT;
                break;

            case Encoding::UCS2BE:
                messageCoding = TP_DCS_UCS2BE;
                break;

            default:
                // Cancel sending
                ModemSerial.write(0x1B);
                return false;
        }

        sendHexToModem(messageCoding);
    }

    // Send message:
    //   byte  0:    TP-UDL (user data length)
    //   bytes 1..n: TP-UD  (user data)
    {
        switch (messageCoding)
        {
            case TP_DCS_GSM7BIT:
            {
                if (messageLength > 160)
                {
                    // Cancel sending
                    ModemSerial.write(0x1B);
                    return false;
                }

                sendHexToModem(messageLength);
                sendGSMAlphaToModem(message, messageLength);
            }
            break;

            case TP_DCS_UCS2BE:
            {
                if (messageLength > 140)
                {
                    // Cancel sending
                    ModemSerial.write(0x1B);
                    return false;
                }

                messageLength &= ~1;

                sendHexToModem(messageLength);

                const char* messageBytePtr = message;
                const char* messageBytePtrEnd = message + messageLength;

                while (messageBytePtr < messageBytePtrEnd)
                {
                    sendHexToModem(*messageBytePtr++);
                    sendHexToModem(*messageBytePtr++);
                }
            }
            break;

            default:
            {
                // Cancel sending
                ModemSerial.write(0x1B);
            }
            return false;
        }
    }

    // Confirm sending
    ModemSerial.write(0x1A);
    return true;
}

bool parseFileEntryFromModem(char* fileNameBuffer, size_t fileNameBufferSize, size_t* fileSize)
{
    if (fileNameBuffer != nullptr &&
        fileNameBufferSize > 0)
    {
        fileNameBuffer[0] = '\0';
    }

    if (fileSize != nullptr)
    {
        *fileSize = 0;
    }

    if (!skipCharactersFromModem(F("+QFLST:")))
    {
        return false;
    }

    // Read or skip file name in double quotes
    {
        if (!skipCharacterFromModem('"'))
        {
            return false;
        }

        if (fileNameBuffer == nullptr)
        {
            if (!skipCharactersFromModemUntil('"'))
            {
                return false;
            }
        }
        else
        {
            if (!readCharactersFromModemUntil(fileNameBuffer, fileNameBufferSize, '"'))
            {
                return false;
            }
        }

        if (!skipCharacterFromModem('"') ||
            !skipCharacterFromModem(','))
        {
            return false;
        }
    }

    // Read or skip file size
    {
        if (fileSize == nullptr)
        {
            if (!skipDigitsFromModem())
            {
                return false;
            }
        }
        else
        {
            char fileSizeBuffer[5+1];

            if (!readDigitsFromModem(fileSizeBuffer, sizeof(fileSizeBuffer)))
            {
                return false;
            }

            *fileSize = atoi(fileSizeBuffer);
        }
    }

    // Discard remaining line (optional RAM size) including end-of-line sequence
    discardLineFromModem();

    return true;
}

unsigned long SerialCommandHelpTime = 0;

void delaySerialCommandHelp(unsigned long delay)
{
    SerialCommandHelpTime = millis() + delay;
}

void printSerialCommandHelp()
{
    Serial.println(F("SMSForwarder: Enter command via serial interface: list, add +491701234567, del +491701234567, voice +491701234567, voice disable"));
}

void printSerialCommandHelpWithDelay()
{
    if (SerialCommandHelpTime == static_cast<unsigned long>(-1))
    {
        return;
    }

    signed long remainingMillis = static_cast<signed long>(SerialCommandHelpTime - millis());

    if (remainingMillis < 0)
    {
        printSerialCommandHelp();
        SerialCommandHelpTime = static_cast<unsigned long>(-1);
    }
}

void executeSerialCommandAdd(const char* argumentsPtr)
{
    const char* destinationPtr = argumentsPtr;

    if (skipCharacterInBuffer(argumentsPtr, '+') &&
        skipCharactersInBuffer(argumentsPtr, isDecDigit, 7, 17) &&
        isEndOfBuffer(argumentsPtr))
    {
        int destinationIndex = findSMSDestination(destinationPtr);

        if (destinationIndex >= 0)
        {
            Serial.print(F("SMSForwarder: Destination phone number already configured in slot "));
            Serial.println(destinationIndex);

            printSMSDestinations();
            return;
        }

        int emptyDestinationIndex = findSMSDestination();

        if (emptyDestinationIndex < 0)
        {
            Serial.println(F("SMSForwarder: No empty slot for new destination phone number"));

            printSMSDestinations();
            return;
        }

        memset(EEPROMConfig.destinations[emptyDestinationIndex], '\0', sizeof(EEPROMConfig.destinations[emptyDestinationIndex]));
        strcpy(EEPROMConfig.destinations[emptyDestinationIndex], destinationPtr);

        EEPROM.put(0, EEPROMConfig);

        Serial.print(F("SMSForwarder: Configured destination phone number in slot "));
        Serial.println(emptyDestinationIndex);

        printSMSDestinations();
    }
    else
    {
        Serial.println(F("SMSForwarder: Invalid destination phone number (expected international phone number format)"));
    }
}

void executeSerialCommandDel(const char* argumentsPtr)
{
    const char* destinationPtr = argumentsPtr;

    int destinationIndex = findSMSDestination(destinationPtr);

    if (destinationIndex < 0)
    {
        Serial.println(F("SMSForwarder: Destination phone number not configured"));

        printSMSDestinations();
        return;
    }

    memset(EEPROMConfig.destinations[destinationIndex], '\0', sizeof(EEPROMConfig.destinations[destinationIndex]));

    EEPROM.put(0, EEPROMConfig);

    // Reset destination runtime state
    DestinationStates[destinationIndex] = DestinationState();

    Serial.print(F("SMSForwarder: Deleted destination phone number from slot "));
    Serial.println(destinationIndex);

    printSMSDestinations();
}

void executeSerialCommandVoice(const char* argumentsPtr)
{
    if (isEndOfBuffer(argumentsPtr))
    {
        queryCallForwardingAsync([](ModemResult modemResult, const char* destination)
        {
            if (modemResult == ModemResult::OK)
            {
                if (destination == nullptr)
                {
                    Serial.println(F("GSM: Voice call fowarding disabled"));
                }
                else
                {
                    Serial.print(F("GSM: Voice call forwarding to "));
                    Serial.println(destination);
                }
            }
            else
            {
                Serial.print(F("GSM: Error querying voice call forwarding: "));
                Serial.println(static_cast<int>(modemResult));
            }
        });

        return;
    }

    const char* parseDisablePtr = argumentsPtr;

    if (skipCharactersInBuffer(parseDisablePtr, F("disable")) &&
        isEndOfBuffer(parseDisablePtr))
    {
        disableCallForwardingAsync([](ModemResult modemResult)
        {
            if (modemResult == ModemResult::OK)
            {
                Serial.println(F("GSM: Successfully disabled voice call forwarding"));
            }
            else
            {
                Serial.print(F("GSM: Error disabling voice call forwarding: "));
                Serial.println(static_cast<int>(modemResult));
            }
        });

        return;
    }

    const char* parseDestinationPtr = argumentsPtr;

    if (skipCharacterInBuffer(parseDestinationPtr, '+') &&
        skipCharactersInBuffer(parseDestinationPtr, isDecDigit, 7, 17) &&
        isEndOfBuffer(parseDestinationPtr))
    {
        updateCallForwardingAsync(argumentsPtr, [](ModemResult modemResult, const char* destination)
        {
            if (modemResult == ModemResult::OK)
            {
                Serial.print(F("GSM: Successfully enabled voice call forwarding to "));
                Serial.println(destination);
            }
            else
            {
                Serial.print(F("GSM: Error updating voice call forwarding: "));
                Serial.println(static_cast<int>(modemResult));
            }
        });

        return;
    }

    Serial.println(F("SMSForwarder: Invalid voice call forwarding number (expected \"+491701234567\" or \"disable\")"));
}
    
void executeSerialCommandAT(const char* command)
{
    struct CustomATModemCommand : ModemCommand
    {
        // Custom modem command to execute
        char command[48+1];

        virtual bool sendCommand() override
        {
            // Send manual AT command to modem
            sendCommandToModem(command, 10000);

            return true;
        }

        virtual bool tryProcessResponse() override
        {
            while (true)
            {
                ModemResult modemResult = parseResultFromModem();
    
                if (modemResult != ModemResult::None)
                {
                    Serial.print(F("SMSForwarder: "));
                    Serial.print(command);
                    Serial.print(F(" command result: "));
                    Serial.println(static_cast<int>(modemResult));
                    break;
                }
    
                if (isModemTimeout())
                {
                    Serial.print(F("SMSForwarder: "));
                    Serial.print(command);
                    Serial.println(F(" command timeout before receiving result code"));
                    break;
                }
        
                Serial.print(F("GSM: "));
                discardLineFromModem(true);
            }
    
            return true;
        }
    };

    CustomATModemCommand* modemCommand = enqueueModemCommand<CustomATModemCommand>();

    if (modemCommand == nullptr)
    {
        Serial.println(F("SMSForwarder: Cannot run custom AT command (modem command queue full)"));
        return;
    }

    size_t commandLength = strlen(command);

    if (commandLength >= sizeof(modemCommand->command))
    {
        cancelModemCommand(modemCommand);

        Serial.println(F("SMSForwarder: Cannot run custom AT command (too long)"));
        return;
    }

    strcpy(modemCommand->command, command);

    modemCommand->state = ModemCommandState::ReadyToSendCommand;
}

void printSMSDestinations()
{
    for (uint8_t destinationIndex = 0; destinationIndex < EEPROMConfigSchema::NumDestinations; ++destinationIndex)
    {
        Serial.print(F("EEPROM: SMS destination "));
        Serial.print(destinationIndex);

        if (EEPROMConfig.destinations[destinationIndex][0] == '\0')
        {
            Serial.println(F(" not configured"));
        }
        else
        {
            Serial.print(F(" configured: "));
            Serial.println(EEPROMConfig.destinations[destinationIndex]);
        }
    }
}

int findSMSDestination()
{
    for (uint8_t destinationIndex = 0; destinationIndex < EEPROMConfigSchema::NumDestinations; ++destinationIndex)
    {
        if (EEPROMConfig.destinations[destinationIndex][0] == '\0')
        {
            return destinationIndex;
        }
    }

    return -1;
}

int findSMSDestination(const char* destination)
{
    for (uint8_t destinationIndex = 0; destinationIndex < EEPROMConfigSchema::NumDestinations; ++destinationIndex)
    {
        const char* characterPtrA = EEPROMConfig.destinations[destinationIndex];
        const char* characterPtrB = destination;

        while (true)
        {
            char characterA = *characterPtrA++;
            char characterB = *characterPtrB++;

            // Support comparing null-terminated and GSMBYTES-terminated strings
            if ((characterA == '\0' || characterA == '\xFF') &&
                (characterB == '\0' || characterB == '\xFF'))
            {
                return destinationIndex;
            }

            if (characterA != characterB)
            {
                break;
            }
        }
    }

    return -1;
}

bool sendSubmitSMSCommand(const char* destination, size_t destinationLength, const char* message, Encoding messageEncoding, uint8_t messageLength)
{
    size_t submitPDUSize = computeSMSPDUSize(destination, destinationLength, messageEncoding, messageLength);

    if (submitPDUSize > 0)
    {
        //*
        sendCommandToModem(F("AT+CMGS="), submitPDUSize, 120000);
        sendSMSPDUToModem(destination, destinationLength, message, messageEncoding, messageLength);
        /*/
        Serial.print(F("SMSForwarder DEBUG: SMS destination: "));
        printlnEncoded(Serial, destination, destinationLength, Encoding::GSMBYTES, true);
        Serial.print(F("SMSForwarder DEBUG: SMS message: "));
        printlnEncoded(Serial, message, messageLength, messageEncoding, true);
        Serial.print(F("SMSForwarder DEBUG: SMS message length: "));
        Serial.println(messageLength);
        fakeReadFromModemRollback(F("+CMGS: 255\r0\r"));
        //*/
        return true;
    }
    else
    {
        Serial.println(F("SMSForwarder: Cannot send SMS because of invalid SMS message data"));
        return false;
    }
}

bool parseSubmitSMSResponse(char* messageReferenceBuffer, size_t messageReferenceBufferSize)
{
    markReadFromModemRollback();
    
    skipEndOfLineFromModem();
    skipEndOfLineFromModem();
    skipCharacterFromModem('>');
    skipCharacterFromModem(' ');

    if (skipCharactersFromModem(F("+CMGS:")) &&
        readDigitsFromModem(messageReferenceBuffer, messageReferenceBufferSize) &&
        skipEndOfLineFromModem())
    {
        releaseReadFromModemRollback();
        return true;
    }
    else
    {
        seekReadFromModemRollback();
        return false;
    }
}

void appendSenderToSMSMessage(const char* sender, size_t senderLength, char* messageBuffer, size_t messageBufferSize, Encoding messageEncoding, uint8_t& messageLengthInOut)
{
    switch (messageEncoding)
    {
        case Encoding::GSMBYTES:
        {
            char* messageCharacterPtr = messageBuffer + messageLengthInOut;
            char* messageCharacterPtrEnd = messageBuffer + min(160, messageBufferSize - sizeof(char));

            if (messageCharacterPtr + senderLength + 2 * sizeof('\n') < messageCharacterPtrEnd)
            {
                *messageCharacterPtr++ = '\n';
                *messageCharacterPtr++ = '\n';

                memcpy(messageCharacterPtr, sender, senderLength);
                messageCharacterPtr += senderLength;
            }

            *messageCharacterPtr = '\xFF';

            messageLengthInOut = messageCharacterPtr - messageBuffer;
        }
        break;

        case Encoding::UCS2BE:
        {
            wchar_t* messageCodepointPtr = reinterpret_cast<wchar_t*>(messageBuffer + messageLengthInOut);
            wchar_t* messageCodepointPtrEnd = reinterpret_cast<wchar_t*>(messageBuffer + min(140, messageBufferSize - sizeof(wchar_t)));

            if (messageCodepointPtr + senderLength + 2 * sizeof('\n') < messageCodepointPtrEnd)
            {
                *messageCodepointPtr++ = swapBytes('\n');
                *messageCodepointPtr++ = swapBytes('\n');

                bool isExtended = false;

                const char* senderCharacterPtr = sender;
                const char* senderCharacterPtrEnd = sender + senderLength;

                for (; senderCharacterPtr < senderCharacterPtrEnd; ++senderCharacterPtr)
                {
                    unsigned char senderCharacter = *senderCharacterPtr;

                    if (senderCharacter > '\x7F')
                    {
                        break;
                    }

                    if (senderCharacter == '\x1B')
                    {
                        isExtended = true;
                        continue;
                    }

                    wchar_t senderCodepoint = lookupUnicodeForGSM(senderCharacter, isExtended);
                    *messageCodepointPtr++ = swapBytes(senderCodepoint);

                    isExtended = false;
                }
            }
            
            *messageCodepointPtr = '\0';

            messageLengthInOut = reinterpret_cast<char*>(messageCodepointPtr) - messageBuffer;
        }
        break;

        default: break;
    }
}

bool tryProcessUnsolicitedReceiveSMS()
{
    markReadFromModemRollback();

    // Identify unsolicited result code: SMS received
    if (skipCharactersFromModem(F("+CMT:")))
    {
        releaseReadFromModemRollback();
    }
    else
    {
        seekReadFromModemRollback();
        return false;
    }

    // Discard trailing <alpha>,<length> fields
    discardLineFromModem();

    Serial.println(F("GSM: Received SMS"));

    // Parsed message sender (always in GSMBYTES encoding)
    char sender[17+1];
    uint8_t senderLength;

    // User data header present before message body?
    bool hasUserDataHeader;

    if (parseSMSPDUHeaderFromModem(
        sender, sizeof(sender),
        senderLength,
        hasUserDataHeader))
    {
        // Parsed SMS header
    }
    else
    {
        discardLineFromModem();
    
        Serial.println(F("SMSForwarder: Unable to parse SMS header"));
        return true;
    }

    int destinationIndex = findSMSDestination(sender);

    if (destinationIndex >= 0)
    {
        Serial.print(F("SMSForwarder: Privileged SMS sender: "));
        printlnEncoded(Serial, sender, sizeof(sender), Encoding::GSMBYTES);

        // Privileged command in SMS message
        char message[32+1];
        Encoding messageEncoding;
        uint8_t messageLength;

        // Parse incoming message into enqueued modem command to forward it
        if (parseSMSPDUBodyFromModem(
            hasUserDataHeader,
            message, sizeof(message),
            messageEncoding,
            messageLength))
        {
            // Parsed SMS message successfully
        }
        else
        {
            discardLineFromModem();
        
            Serial.println(F("SMSForwarder: Unable to parse privileged SMS body"));
            return true;
        }

        Serial.print(F("SMSForwarder: Privileged SMS message: "));
        printlnEncoded(Serial, message, messageLength, messageEncoding, true);

        const char* parseMessagePtr = message;
        const char* parseMessagePtrEnd = message + sizeof(message);

        if (skipHorizontalWhitespaceInBuffer(parseMessagePtr, parseMessagePtrEnd) &&
            skipCharactersInBuffer(parseMessagePtr, F("Anrufe"), false) &&
            skipHorizontalWhitespaceInBuffer(parseMessagePtr, parseMessagePtrEnd, 1) &&
            skipCharactersInBuffer(parseMessagePtr, F("an"), false) &&
            skipHorizontalWhitespaceInBuffer(parseMessagePtr, parseMessagePtrEnd, 1) &&
            skipCharactersInBuffer(parseMessagePtr, F("mich"), false) &&
            skipHorizontalWhitespaceInBuffer(parseMessagePtr, parseMessagePtrEnd) &&
            skipCharacterInBuffer(parseMessagePtr, '\xFF'))
        {
            Serial.print(F("SMSForwarder: Privileged SMS requests voice call forwarding to "));
            Serial.println(EEPROMConfig.destinations[destinationIndex]);

            if (DestinationStates[destinationIndex].interactionInProgress)
            {
                Serial.println(F("SMSForwarder: Ignoring privileged SMS request because a prior interaction is already in progress"));
                return true;
            }

            struct UpdateCallForwardingSMSModemCommand : ModemCommand
            {
                // Telephone number of the requestor and requested updated call forwarding number
                char destination[17+1];

                // Destination index for broadcasted confirmation SMS after call forwarding change
                uint8_t messageDestinationIndex;

                enum struct Step : uint8_t
                {
                    UpdateCallForwarding,
                    SendSuccessMessage,
                    SendFailureMessage,
                    Done,

                    ErrorUpdateCallForwarding,
                };

                Step step = Step::UpdateCallForwarding;

                UpdateCallForwardingSMSModemCommand(uint8_t destinationIndex)
                {
                    strcpy(destination, EEPROMConfig.destinations[destinationIndex]);
                    DestinationStates[destinationIndex].interactionInProgress = true;

                    messageDestinationIndex = 0;
                }

                virtual ~UpdateCallForwardingSMSModemCommand() override
                {
                    int destinationIndex = findSMSDestination(destination);
                    if (destinationIndex >= 0)
                    {
                        DestinationStates[destinationIndex].interactionInProgress = false;
                    }
                }

                virtual bool sendCommand() override
                {
                    switch (step)
                    {
                        case Step::UpdateCallForwarding:
                        {
                            sendUpdateCallForwardingCommand(destination);
                        }
                        return true;

                        case Step::SendSuccessMessage:
                        case Step::SendFailureMessage:
                        {
                            const char* messageDestination;
                            char message[64+1];

                            BufferPrint messagePrint(message, sizeof(message));

                            if (step == Step::SendSuccessMessage)
                            {
                                messageDestination = EEPROMConfig.destinations[messageDestinationIndex];

                                if (messageDestination[0] == '\0')
                                {
                                    return false;
                                }

                                if (strcmp(messageDestination, destination) == 0)
                                {
                                    messagePrint.print(F("Anrufe werden jetzt an dich weitergeleitet."));
                                }
                                else
                                {
                                    messagePrint.print(F("Anrufe werden jetzt an "));
                                    messagePrint.print(destination);
                                    messagePrint.print(F(" weitergeleitet."));
                                }
                            }
                            else
                            {
                                messageDestination = destination;

                                messagePrint.print(F("Anrufweiterleitung konnte nicht angepasst werden."));
                            }

                            size_t messageDestinationLength = strlen(messageDestination);
                            size_t messageLength = strlen(message);
                            return sendSubmitSMSCommand(messageDestination, messageDestinationLength, message, Encoding::GSMBYTES, messageLength);
                        }

                        default: break;
                    }

                    return false;
                }

                virtual bool tryProcessResponse() override
                {
                    switch (step)
                    {
                        case Step::UpdateCallForwarding:
                        {
                            ModemResult modemResult = parseResultFromModem();

                            if (modemResult == ModemResult::None)
                            {
                                return false;
                            }

                            if (modemResult == ModemResult::OK)
                            {
                                Serial.print(F("SMSForwarder: Updated voice call forwarding for privileged SMS call forwarding update request to "));
                                Serial.println(destination);
                            }
                            else
                            {
                                step = Step::ErrorUpdateCallForwarding;

                                Serial.print(F("SMSForwarder: Error updating voice call forwarding for privileged SMS call forwarding update request: "));
                                Serial.println(static_cast<int>(modemResult));
                            }
                        }
                        return true;

                        case Step::SendSuccessMessage:
                        case Step::SendFailureMessage:
                        {
                            char messageReference[3+1];

                            if (parseSubmitSMSResponse(messageReference, sizeof(messageReference)))
                            {
                                const char* messageDestination = EEPROMConfig.destinations[messageDestinationIndex];

                                Serial.print(F("SMSForwarder: Successfully sent privileged SMS result message to "));
                                Serial.print(messageDestination);
                                Serial.print(F(" (message reference: "));
                                Serial.print(messageReference);
                                Serial.println(F(")"));
                            }

                            ModemResult modemResult = parseResultFromModem();
        
                            if (modemResult == ModemResult::None)
                            {
                                return false;
                            }
                
                            if (modemResult != ModemResult::OK)
                            {
                                Serial.print(F("SMSForwarder: Error sending privileged SMS result message: "));
                                Serial.println(static_cast<int>(modemResult));
                            }

                            return true;
                        }
                        break;

                        default: break;
                    }

                    return false;
                }

                virtual bool advanceToNextCommand() override
                {
                    switch (step)
                    {
                        case Step::UpdateCallForwarding:
                            step = Step::SendSuccessMessage;
                            return true;
                        
                        case Step::ErrorUpdateCallForwarding:
                            step = Step::SendFailureMessage;
                            return true;
                        
                        case Step::SendSuccessMessage:
                            messageDestinationIndex++;
                            step = (messageDestinationIndex < EEPROMConfigSchema::NumDestinations)
                                ? Step::SendSuccessMessage
                                : Step::Done;
                            return step != Step::Done;

                        case Step::SendFailureMessage:
                            step = Step::Done;
                            return false;

                        default: break;
                    }

                    return false;
                }
            };

            UpdateCallForwardingSMSModemCommand* modemCommand = enqueueModemCommand<UpdateCallForwardingSMSModemCommand>(destinationIndex);

            if (modemCommand == nullptr)
            {
                Serial.println(F("SMSForwarder: Cannot send response to privileged SMS call forwarding update request (modem command queue full)"));
                return true;
            }

            modemCommand->state = ModemCommandState::ReadyToSendCommand;

            return true;
        }
        else
        {
            Serial.println(F("SMSForwarder: Privileged SMS requests status and help information"));

            if (DestinationStates[destinationIndex].interactionInProgress)
            {
                Serial.println(F("SMSForwarder: Ignoring privileged SMS request because a prior interaction is already in progress"));
                return true;
            }

            struct InfoSMSModemCommand : ModemCommand
            {
                // Telephone number of the help and info requestor
                char destination[17+1];

                // Current updated call forwarding number queried from the network
                char callForwardingDestination[17+1];

                enum struct Step : uint8_t
                {
                    QueryCallForwarding,
                    SendInfoAndHelpMessage,
                    Done,

                    ErrorQueryCallForwarding,
                    ErrorSendInfoAndHelpMessage,
                };

                Step step = Step::QueryCallForwarding;

                InfoSMSModemCommand(uint8_t destinationIndex)
                {
                    strcpy(destination, EEPROMConfig.destinations[destinationIndex]);
                    DestinationStates[destinationIndex].interactionInProgress = true;
                }

                virtual ~InfoSMSModemCommand() override
                {
                    int destinationIndex = findSMSDestination(destination);
                    if (destinationIndex >= 0)
                    {
                        DestinationStates[destinationIndex].interactionInProgress = false;
                    }
                }

                virtual bool sendCommand() override
                {
                    switch (step)
                    {
                        case Step::QueryCallForwarding:
                        {
                            sendQueryCallForwardingCommand();
                        }
                        return true;

                        case Step::SendInfoAndHelpMessage:
                        {
                            bool destinationIsCallForwardingDestination = strcmp(destination, callForwardingDestination) == 0;

                            char message[160+1];
                            
                            BufferPrint messagePrint(message, sizeof(message));
                            
                            if (destinationIsCallForwardingDestination)
                            {
                                messagePrint.print(F("Anrufe gehen an dich."));
                            }
                            else
                            {
                                messagePrint.print(F("Anrufe:\n"));
                                messagePrint.print(callForwardingDestination);
                            }

                            messagePrint.print(F("\n\n"));
                            messagePrint.print(F("SMS:"));

                            for (const char* destination : EEPROMConfig.destinations)
                            {
                                if (destination[0] != '\0')
                                {
                                    messagePrint.print('\n');
                                    messagePrint.print(destination);
                                }
                            }

                            if (!destinationIsCallForwardingDestination)
                            {
                                messagePrint.print(F("\n\n"));
                                messagePrint.print(F("Antworte \"Anrufe an mich\", um Anrufe an dich weiterzuleiten."));
                            }

                            size_t destinationLength = strlen(destination);
                            size_t messageLength = strlen(message);
                            return sendSubmitSMSCommand(destination, destinationLength, message, Encoding::GSMBYTES, messageLength);
                        }

                        default: break;
                    }

                    return false;
                }

                virtual bool tryProcessResponse() override
                {
                    switch (step)
                    {
                        case Step::QueryCallForwarding:
                        {
                            if (parseQueryCallForwardingResponse(callForwardingDestination, sizeof(callForwardingDestination)))
                            {
                                // Parsed call forwarding destination successfully
                            }
                            else
                            {
                                // Not the expected response
                                return false;
                            }

                            ModemResult modemResult = parseResultFromModem();

                            if (modemResult == ModemResult::OK)
                            {
                                Serial.print(F("SMSForwarder: Queried voice call forwarding for privileged SMS info request: "));
                                Serial.println(callForwardingDestination);
                            }
                            else
                            {
                                step = Step::ErrorQueryCallForwarding;

                                Serial.print(F("SMSForwarder: Error querying voice call forwarding for privileged SMS info request: "));
                                Serial.println(static_cast<int>(modemResult));
                            }
                        }
                        return true;

                        case Step::SendInfoAndHelpMessage:
                        {
                            char messageReference[3+1];

                            if (parseSubmitSMSResponse(messageReference, sizeof(messageReference)))
                            {
                                Serial.print(F("SMSForwarder: Successfully sent privileged SMS info response to "));
                                Serial.print(destination);
                                Serial.print(F(" (message reference: "));
                                Serial.print(messageReference);
                                Serial.println(F(")"));
                            }

                            ModemResult modemResult = parseResultFromModem();
        
                            if (modemResult == ModemResult::None)
                            {
                                return false;
                            }
                
                            if (modemResult != ModemResult::OK)
                            {
                                Serial.print(F("SMSForwarder: Error sending privileged SMS info response: "));
                                Serial.println(static_cast<int>(modemResult));
                            }

                            return true;
                        }
                        break;

                        default: break;
                    }

                    return false;
                }

                virtual bool advanceToNextCommand() override
                {
                    switch (step)
                    {
                        case Step::QueryCallForwarding:
                            step = Step::SendInfoAndHelpMessage;
                            return true;
                        
                        case Step::SendInfoAndHelpMessage:
                            step = Step::Done;
                            return true;

                        default: break;
                    }

                    return false;
                }
            };

            InfoSMSModemCommand* modemCommand = enqueueModemCommand<InfoSMSModemCommand>(destinationIndex);

            if (modemCommand == nullptr)
            {
                Serial.println(F("SMSForwarder: Cannot send response to privileged SMS info request (modem command queue full)"));
                return true;
            }

            modemCommand->state = ModemCommandState::ReadyToSendCommand;

            return true;
        }

        discardLineFromModem();
        return true;
    }
    else
    {
        Serial.print(F("SMSForwarder: External SMS sender: "));
        printlnEncoded(Serial, sender, sizeof(sender), Encoding::GSMBYTES);
    
        struct ForwardSMSModemCommand : ModemCommand
        {
            // Forwarded SMS message content including original sender
            char message[160+1];
            Encoding messageEncoding;
            uint8_t messageLength;
    
            // Next destination index to forward SMS to
            uint8_t destinationIndex = 0;
    
            virtual bool sendCommand() override
            {
                const char* destination = EEPROMConfig.destinations[destinationIndex];
    
                if (destination[0] == '\0')
                {
                    return false;
                }
    
                Serial.print(F("SMSForwarder: Forwarding SMS to "));
                Serial.println(destination);
    
                size_t destinationLength = strlen(destination);
                return sendSubmitSMSCommand(destination, destinationLength, message, messageEncoding, messageLength);
            }
    
            virtual bool tryProcessResponse() override
            {
                char messageReference[3+1];

                if (parseSubmitSMSResponse(messageReference, sizeof(messageReference)))
                {
                    Serial.print(F("SMSForwarder: Successfully forwarded SMS to "));
                    Serial.print(EEPROMConfig.destinations[destinationIndex]);
                    Serial.print(F(" (message reference: "));
                    Serial.print(messageReference);
                    Serial.println(F(")"));
                }
        
                ModemResult modemResult = parseResultFromModem();
        
                if (modemResult == ModemResult::None)
                {
                    return false;
                }
    
                if (modemResult != ModemResult::OK)
                {
                    Serial.print(F("SMSForwarder: Error forwarding SMS: "));
                    Serial.println(static_cast<int>(modemResult));
                }
    
                return true;
            }
    
            virtual bool advanceToNextCommand() override
            {
                destinationIndex++;
                return destinationIndex < EEPROMConfigSchema::NumDestinations;
            }
        };
    
        ForwardSMSModemCommand* modemCommand = enqueueModemCommand<ForwardSMSModemCommand>();
    
        if (modemCommand == nullptr)
        {
            discardLineFromModem();
    
            Serial.println(F("SMSForwarder: Cannot forward SMS (modem command queue full)"));
            return true;
        }

        // Parse incoming message into enqueued modem command to forward it
        if (parseSMSPDUBodyFromModem(
            hasUserDataHeader,
            modemCommand->message, sizeof(modemCommand->message),
            modemCommand->messageEncoding,
            modemCommand->messageLength))
        {
            // Parsed SMS message successfully
        }
        else
        {
            discardLineFromModem();
            cancelModemCommand(modemCommand);
        
            Serial.println(F("SMSForwarder: Unable to parse external SMS body"));
            return true;
        }

        Serial.print(F("SMSForwarder: External SMS message: "));
        printlnEncoded(Serial, modemCommand->message, modemCommand->messageLength, modemCommand->messageEncoding, true);
    
        appendSenderToSMSMessage(
            sender,
            senderLength, 
            modemCommand->message, sizeof(modemCommand->message),
            modemCommand->messageEncoding,
            modemCommand->messageLength);
        
        Serial.print(F("SMSForwarder: Forwarded SMS message: "));
        printlnEncoded(Serial, modemCommand->message, modemCommand->messageLength, modemCommand->messageEncoding, true);
    
        modemCommand->state = ModemCommandState::ReadyToSendCommand;
        return true;
    }
}

void setup()
{
    delay(1000);

    setupLED();
    setupSerial();

    Serial.println(F("SMSForwarder startup"));

    setupEEPROM();
    setupModem();
}

void setupLED()
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
}

void setupSerial()
{
    Serial.begin(57600);
    discardRead(Serial);
}

void setupEEPROM()
{
    EEPROM.get(0, EEPROMConfig);

    if (EEPROMConfig.schema == EEPROMConfigSchema::ExpectedSchema &&
        EEPROMConfig.version == EEPROMConfigSchema::ExpectedVersion)
    {
        Serial.println(F("EEPROM: Loaded configuration"));
    }
    else
    {
        if (EEPROMConfig.schema != EEPROMConfigSchema::ExpectedSchema)
        {
            Serial.println(F("EEPROM: Unable to load configuration (unknown schema)"));
        }
        else if (EEPROMConfig.version != EEPROMConfigSchema::ExpectedVersion)
        {
            Serial.print(F("EEPROM: Unable to load configuration (unknown version: expected "));
            Serial.print(EEPROMConfigSchema::ExpectedVersion);
            Serial.print(F(", loaded "));
            Serial.print(EEPROMConfig.version);
            Serial.println(F(")"));
        }

        memset(&EEPROMConfig, '\0', sizeof(EEPROMConfig));

        EEPROMConfig.schema = EEPROMConfigSchema::ExpectedSchema;
        EEPROMConfig.version = EEPROMConfigSchema::ExpectedVersion;
    }

    if (EEPROMConfig.SIMPIN[0] == '\0')
    {
        Serial.println(F("EEPROM: SIM PIN not configured"));
    }
    else
    {
        Serial.println(F("EEPROM: SIM PIN configured"));
    }

    printSMSDestinations();
}

ModemRuntimeState ModemState = ModemRuntimeState::Uninitialized;

void setupModem()
{
    ModemSerial.begin(9600);
    discardRead(ModemSerial);

    pinMode(MODEM_RESET_PIN, OUTPUT);
    digitalWrite(MODEM_RESET_PIN, HIGH);
    pinMode(MODEM_POWER_PIN, OUTPUT);
    digitalWrite(MODEM_POWER_PIN, HIGH);
    pinMode(MODEM_STATUS_PIN, INPUT);

    delay(1000);

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
        setupReceiveSMS())
    {
        ModemState = ModemRuntimeState::Authorize;
    }

    if (ModemState == ModemRuntimeState::Authorize &&
        enterSIMPIN(EEPROMConfig.SIMPIN))
    {
        ModemState = ModemRuntimeState::Active;
    }

    switch (ModemState)
    {
        case ModemRuntimeState::Uninitialized:
            Serial.println(F("GSM: Startup failed"));
            break;
        
        case ModemRuntimeState::Authorize:
            Serial.println(F("GSM: Enter SIM PIN via serial interface to complete startup"));
            break;

        case ModemRuntimeState::Active:
            Serial.println(F("GSM: Startup complete"));
            break;
    }
}

void loop()
{
    printSerialCommandHelpWithDelay();

    bool isActive;

    isActive = loopModem();
    isActive = loopSerial() || isActive;

    if (isActive)
    {
        // Print serial command help with delay after activity
        delaySerialCommandHelp(10000);
    }
}

char SerialCommandBuffer[48+1];
uint8_t SerialCommandLength = 0;

bool loopSerial()
{
    int maybeCharacter = Serial.read();

    if (maybeCharacter < 0)
    {
        return false;
    }

    char character = static_cast<char>(maybeCharacter);

    if (isHorizontalWhitespace(character) && (SerialCommandLength == 0 || isHorizontalWhitespace(SerialCommandBuffer[SerialCommandLength - 1])))
    {
        return false;
    }

    if (character == '\r' ||
        character == '\n')
    {
        if (SerialCommandLength == 0)
        {
            return false;
        }

        if (isHorizontalWhitespace(SerialCommandBuffer[SerialCommandLength - 1]))
        {
            SerialCommandLength--;
        }

        if (SerialCommandLength > 0)
        {
            SerialCommandBuffer[SerialCommandLength] = '\0';
            
            Serial.print(F("SMSForwarder: Entered command: "));
            Serial.println(SerialCommandBuffer);

            switch (ModemState)
            {
                case ModemRuntimeState::Uninitialized:
                {
                    Serial.println(F("SMSForwarder: Startup failed, no commands accepted"));
                }
                break;

                case ModemRuntimeState::Authorize:
                {
                    const char* serialCommandCharacterPtr = SerialCommandBuffer;

                    if (skipCharactersInBuffer(serialCommandCharacterPtr, isDecDigit, 4, 4) &&
                        isEndOfBuffer(serialCommandCharacterPtr))
                    {
                        if (enterSIMPIN(SerialCommandBuffer))
                        {
                            strcpy(EEPROMConfig.SIMPIN, SerialCommandBuffer);
                            EEPROM.put(0, EEPROMConfig);

                            Serial.println("EEPROM: Updated configured SIM PIN");

                            ModemState = ModemRuntimeState::Active;
                        }
                    }
                    else
                    {
                        Serial.println(F("SMSForwarder: Invalid format for SIM PIN (expected four decimal digits)"));
                    }

                }
                break;

                case ModemRuntimeState::Active:
                {
                    const char* serialCommandCharacterPtr = SerialCommandBuffer;

                    if (skipCharactersInBuffer(serialCommandCharacterPtr, F("list")) &&
                        isEndOfBuffer(serialCommandCharacterPtr))
                    {
                        printSMSDestinations();
                        break;
                    }

                    if (skipCharactersInBuffer(serialCommandCharacterPtr, F("add")) &&
                        isNonWordCharacterInBuffer(serialCommandCharacterPtr))
                    {
                        skipCharactersInBuffer(serialCommandCharacterPtr, isHorizontalWhitespace, 1, 1);
                        executeSerialCommandAdd(serialCommandCharacterPtr);
                        break;
                    }

                    if (skipCharactersInBuffer(serialCommandCharacterPtr, F("del")) &&
                        isNonWordCharacterInBuffer(serialCommandCharacterPtr))
                    {
                        skipCharactersInBuffer(serialCommandCharacterPtr, isHorizontalWhitespace, 1, 1);
                        executeSerialCommandDel(serialCommandCharacterPtr);
                        break;
                    }

                    if (skipCharactersInBuffer(serialCommandCharacterPtr, F("voice")) &&
                        isNonWordCharacterInBuffer(serialCommandCharacterPtr))
                    {
                        skipCharactersInBuffer(serialCommandCharacterPtr, isHorizontalWhitespace, 1, 1);
                        executeSerialCommandVoice(serialCommandCharacterPtr);
                        break;
                    }

                    if (skipCharactersInBuffer(serialCommandCharacterPtr, F("AT")))
                    {
                        executeSerialCommandAT(SerialCommandBuffer);
                        break;
                    }

                    Serial.println(F("SMSForwarder: Unknown command"));
                }
                break;
            }

            SerialCommandLength = 0;
        }

        return true;
    }

    if (SerialCommandLength < sizeof(SerialCommandBuffer) - 1)
    {
        SerialCommandBuffer[SerialCommandLength++] = character;
    }

    return false;
}

bool loopModem()
{
    if (ModemState != ModemRuntimeState::Active)
    {
        return false;
    }

    ModemCommand* modemCommand = peekModemCommand();

    // Pending modem command was abandoned before completing initialization
    if (modemCommand != nullptr && modemCommand->state == ModemCommandState::Uninitialized)
    {
        Serial.print(F("SMSForwarder: Discarding uninitialized modem command"));

        dequeueModemCommand();
        return true;
    }

    if (peekCharacterFromModem() == ModemSerialState::NoData)
    {
        // Send pending modem command
        if (modemCommand != nullptr && modemCommand->state == ModemCommandState::ReadyToSendCommand)
        {
            // Send command, then wait for expected response
            if (modemCommand->sendCommand())
            {
                modemCommand->state = ModemCommandState::ReadyToProcessResponse;
                modemCommand->responseTimeoutTime = ModemTimeoutTime;
                return true;
            }
            
            // No command sent but there is more work
            if (modemCommand->advanceToNextCommand())
            {
                return true;
            }

            // No command sent and there is no more work
            dequeueModemCommand();
            return true;
        }

        // No data available and no pending modem command
        return false;
    }

    startModemTimeout(1000);

    // Process unsolicited result when SMS received
    if (tryProcessUnsolicitedReceiveSMS())
    {
        return true;
    }

    // Process expected response to current modem command
    if (modemCommand != nullptr && modemCommand->state == ModemCommandState::ReadyToProcessResponse)
    {
        ModemTimeoutTime = modemCommand->responseTimeoutTime;

        // Try to process the response until timeout is reached
        if (modemCommand->tryProcessResponse() || isModemTimeout())
        {
            // Response processed (or timeout reached), and there is more work
            if (modemCommand->advanceToNextCommand())
            {
                modemCommand->state = ModemCommandState::ReadyToSendCommand;
                modemCommand->responseTimeoutTime = 0;
                return true;
            }

            // Response processed and there is no more work
            dequeueModemCommand();
            return true;
        }
    }

    // Data available but neither unsolicited result nor expected response
    discardLineFromModem();
    return false;
}
