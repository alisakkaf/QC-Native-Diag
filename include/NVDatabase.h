#ifndef NVDATABASE_H
#define NVDATABASE_H

#include <QString>
#include <QMap>
#include <QStringList>

// NV Item Structure
struct NVItem {
    uint16_t id;
    QString name;
    QString category;
    QString description;
    uint16_t size;
    bool isExtended;
    uint16_t maxIndex;
    QString dataType; // "hex", "bcd", "ascii", "binary"
    bool readOnly;
    
    NVItem() : id(0), size(0), isExtended(false), maxIndex(0), readOnly(false) {}
    
    NVItem(uint16_t _id, QString _name, QString _cat, QString _desc, 
           uint16_t _size, bool _ext = false, uint16_t _idx = 0, 
           QString _type = "hex", bool _ro = false)
        : id(_id), name(_name), category(_cat), description(_desc),
          size(_size), isExtended(_ext), maxIndex(_idx), dataType(_type), readOnly(_ro) {}
};

// NV Database Class
class NVDatabase
{
public:
    static NVDatabase& instance() {
        static NVDatabase db;
        return db;
    }
    
    const NVItem* getItem(uint16_t id) const {
        auto it = m_items.constFind(id);
        return (it != m_items.constEnd()) ? &(*it) : nullptr;
    }
    
    QList<NVItem> getAllItems() const {
        return m_items.values();
    }
    
    QList<NVItem> getItemsByCategory(const QString &category) const {
        QList<NVItem> result;
        for (const NVItem &item : m_items.values()) {
            if (item.category == category) {
                result.append(item);
            }
        }
        return result;
    }
    
    QStringList getCategories() const {
        return m_categories;
    }
    
    QList<NVItem> searchItems(const QString &query) const {
        QList<NVItem> result;
        QString lowerQuery = query.toLower();
        for (const NVItem &item : m_items.values()) {
            if (item.name.toLower().contains(lowerQuery) ||
                item.description.toLower().contains(lowerQuery) ||
                QString::number(item.id).contains(query)) {
                result.append(item);
            }
        }
        return result;
    }

private:
    QMap<uint16_t, NVItem> m_items;
    QStringList m_categories;
    
    NVDatabase() {
        initializeDatabase();
    }
    
    void initializeDatabase() {
        m_categories << "Identity" << "Network" << "Security" << "Radio" 
                     << "System" << "User" << "Advanced";
        
        // === Identity Items ===
        addItem(0, "ESN", "Identity", "Electronic Serial Number", 8, false, 0, "binary", true);
        addItem(1, "MEID", "Identity", "Mobile Equipment Identifier", 14, false, 0, "hex", true);
        addItem(550, "IMEI 1", "Identity", "Primary IMEI (Extended)", 9, false, 0, "bcd");
        addItem(2497, "IMEI 2 (Alt1)", "Identity", "Secondary IMEI Alternative 1", 9, false, 0, "bcd");
        addItem(5014, "IMEI 2 (Alt2)", "Identity", "Secondary IMEI Alternative 2", 9, false, 0, "bcd");
        addItem(1192, "IMSI", "Identity", "International Mobile Subscriber Identity", 9, false, 0, "bcd");
        addItem(2824, "IMSI (M)", "Identity", "IMSI_M for multiple SIM", 9, false, 0, "bcd");
        addItem(71, "SIM_NAME", "Identity", "SIM Card Name/Operator", 16, false, 0, "ascii");
        
        // === Network Items ===
        addItem(176, "MCC", "Network", "Mobile Country Code", 2, false, 0, "binary"); // Was BCD - Fixed to binary
        addItem(177, "MNC", "Network", "Mobile Network Code", 2, false, 0, "binary"); // Was BCD - Fixed to binary
        addItem(85, "Home SID", "Network", "Home System ID", 2, false, 0, "binary");
        addItem(86, "Home NID", "Network", "Home Network ID", 2, false, 0, "binary");
        addItem(161, "MIN1", "Network", "Mobile Identification Number 1", 4, false, 0, "bcd");
        addItem(162, "MIN2", "Network", "Mobile Identification Number 2", 2, false, 0, "bcd");
        addItem(10, "MDN (Phone Number)", "Network", "Mobile Directory Number", 10, false, 0, "ascii");
        addItem(178, "MDN_BCD", "Network", "MDN in BCD format", 10, false, 0, "bcd");
        addItem(259, "PRL Version", "Network", "Preferred Roaming List Version", 2, false, 0, "binary");
        addItem(71, "PRL Enabled", "Network", "PRL Enabled Status", 1, false, 0, "binary");
        addItem(442, "SID/NID Pairs", "Network", "System ID / Network ID Pairs", 40, false, 0, "binary");
        
        // === SIM Card Items ===
        addItem(4105, "ICCID", "Network", "SIM Card Serial Number", 10, true, 0, "bcd");
        addItem(4106, "SIM State", "Network", "SIM Card State", 1, true, 0, "binary");
        addItem(4107, "SIM Type", "Network", "SIM Card Type", 1, true, 0, "binary");
        
        // === Security Items ===
        addItem(85, "NAM Lock", "Security", "NAM Lock Status", 1, false, 0, "binary");
        addItem(6, "Security Code", "Security", "Phone Lock Code", 2, false, 0, "binary");
        addItem(466, "Service Programming Lock", "Security", "SP Lock Status", 1, false, 0, "binary");
        addItem(16, "SPC", "Security", "Service Programming Code", 6, false, 0, "ascii");
        
        // === Radio Items ===
        addItem(10, "Band Class", "Radio", "Current Band Class", 2, false, 0, "binary");
        addItem(441, "Band Preference", "Radio", "Preferred Band Configuration", 8, false, 0, "binary");
        addItem(1877, "RF Cal Date", "Radio", "RF Calibration Date", 6, false, 0, "bcd");
        addItem(6828, "LTE Band Pref", "Radio", "LTE Band Preference (64-bit)", 8, false, 0, "binary");
        addItem(6829, "LTE Band Pref Ext", "Radio", "LTE Band Preference Extended", 16, false, 0, "binary");
        addItem(2954, "RF_BC_CONFIG", "Radio", "RF Band Class Configuration", 256, false, 0, "binary");
        
        // === System Items ===
        addItem(447, "Mob Firmware Rev", "System", "Mobile Firmware Revision", 2, false, 0, "binary");
        addItem(2825, "SW Version", "System", "Software Version String", 20, false, 0, "ascii");
        addItem(5, "MOB_CAI_REV", "System", "Mobile CAI Revision", 1, false, 0, "binary");
        addItem(4, "MOB_MODEL", "System", "Mobile Model", 1, false, 0, "binary");
        addItem(3, "MOB_TERM_HOME", "System", "Home Terminal", 5, false, 0, "binary");
        addItem(2, "MOB_TERM_FOR_SID", "System", "Foreign SID", 5, false, 0, "binary");
        addItem(1, "MOB_TERM_FOR_NID", "System", "Foreign NID", 5, false, 0, "binary");
        addItem(71, "Banner", "System", "Device Banner/Name", 16, false, 0, "ascii");
        
        // === User Items ===
        addItem(6, "Auto Answer", "User", "Auto Answer Configuration", 1, false, 0, "binary");
        addItem(11, "Auto Lock", "User", "Auto Lock Timer", 2, false, 0, "binary");
        addItem(14, "Air Timer", "User", "Total Air Time", 4, false, 0, "binary");
        addItem(15, "Roam Timer", "User", "Total Roaming Time", 4, false, 0, "binary");
        addItem(67, "Life Timer", "User", "Device Life Timer", 4, false, 0, "binary", true);
        addItem(13, "Call Count", "User", "Total Call Count", 2, false, 0, "binary");
        
        // === Advanced Items ===
        addItem(429, "Data Scrubbing", "Advanced", "Data Scrubbing Status", 1, false, 0, "binary");
        addItem(10, "CDMA Channel", "Advanced", "CDMA Channel Number", 2, false, 0, "binary");
        addItem(21, "CDMA SID", "Advanced", "CDMA System ID", 2, false, 0, "binary");
        addItem(22, "CDMA NID", "Advanced", "CDMA Network ID", 2, false, 0, "binary");
        addItem(8, "CDMA RX State", "Advanced", "CDMA Receive State", 1, false, 0, "binary");
        addItem(9, "CDMA Good Frames", "Advanced", "Good Frames Counter", 2, false, 0, "binary");
        addItem(259, "Analog Home SID", "Advanced", "Analog Home System ID", 2, false, 0, "binary");
        addItem(553, "RF Config", "Advanced", "RF Configuration", 4, false, 0, "binary");
        addItem(4964, "WiFi MAC", "Advanced", "WiFi MAC Address", 6, true, 0, "hex");
        addItem(4965, "BT Address", "Advanced", "Bluetooth Address", 6, true, 0, "hex");
    }
    
    void addItem(uint16_t id, QString name, QString cat, QString desc, 
                 uint16_t size, bool ext, uint16_t idx, QString type, bool ro = false) {
        m_items[id] = NVItem(id, name, cat, desc, size, ext, idx, type, ro);
    }
};

#endif // NVDATABASE_H
