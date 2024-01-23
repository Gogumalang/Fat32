#include <iostream>
#include <fstream>

#include "byte_buffer.hpp"

using namespace std;
using namespace sys::io;

class Superblock
{
public:
  Superblock(uint8_t *buffer, int size)
  {
    byte_buffer bb(buffer, 0, size);

    bb.offset(0x0b);
    BytesPerSector = bb.get_uint16_le();
    SectorPerCluster = bb.get_uint8();
    ReservecSectorCount = bb.get_uint16_le();
    NumOfFat = bb.get_uint8();

    bb.offset(0x24);
    TotalSector = bb.get_uint32_le();
    bb.offset(0x2c);
    RootDirCluster = bb.get_uint32_le();
    
    ClusterSize = BytesPerSector * SectorPerCluster;
    FatOffset = ReservecSectorCount * BytesPerSector;
    DataAreaOffset = FatOffset + NumOfFat * TotalSector * BytesPerSector;
    FatSize = TotalSector * BytesPerSector;
  }

public:
  uint32_t ClusterSize;              // 0x1000 : 4k Byte 
  uint64_t FatOffset;                // 0x21C500
  uint64_t DataAreaOffset;           // 0x400000
  uint32_t FatSize;

  uint16_t BytesPerSector;           // 0x200 : 512Byte
  uint8_t SectorPerCluster;          // 0x8 
  uint16_t ReservecSectorCount;      // 
  uint8_t NumOfFat;                  //
  uint32_t TotalSector;              // 0x749
  uint32_t RootDirCluster;           // 2
};

class FatArea
{
public:
  FatArea(uint8_t *buffer, int size)
  {
    byte_buffer bb(buffer, 0, size);
    entry_count = size / 4;
    fat = (uint32_t *)malloc(sizeof(uint32_t) * entry_count);
    for (int i = 0; i < entry_count; i++)
    {
      fat[i] = bb.get_uint32_le();
    }
  }
  int entry_count;
  uint32_t *fat;
};

class DirectoryEntry
{
public:
  DirectoryEntry(uint8_t *buffer, int size, FatArea fatArea)
  {
    byte_buffer bb(buffer, 0, size);

    Name = bb.get_ascii(0x8);
    Name.erase(remove_if(Name.begin(), Name.end(), ::isspace), Name.end());
    Extension = bb.get_ascii(0x3);
    Extension.erase(remove_if(Extension.begin(), Extension.end(), ::isspace), Extension.end());
    FileName = Name + "." + Extension;
    Attribute = bb.get_uint8();

    bb.offset(0x14);
    HighClusterNum = bb.get_uint16_le();
    ClusterNum = HighClusterNum << 16;

    bb.offset(0x1A);
    LowClusterNum = bb.get_uint16_le();
    ClusterNum = ClusterNum | LowClusterNum;
    FileSize = bb.get_uint32_le();

    clusters.push_back(ClusterNum);

    while (fatArea.fat[ClusterNum] != 0xfffffff)
    {
      clusters.push_back(fatArea.fat[ClusterNum]);
      ClusterNum++;
    }
  }

  void export_to(ifstream *file)
  {
    ofstream ofs(FileName, std::ios_base::binary);
    uint32_t RestOfFile = FileSize;
    for (uint32_t cluster : clusters)
    {   
      uint32_t physical_addr = 0x400000 + (cluster - 2) * 0x1000;
      file->seekg(physical_addr);
      char buffer[0x1000] = {0};
      uint32_t bufferSize = (RestOfFile >= 0x1000) ? 0x1000 : RestOfFile;
      file->read(buffer, bufferSize);
      ofs.write(buffer, bufferSize);
      RestOfFile -= 0x1000;
    }
  }
public : 
  string FileName;
  uint32_t ClusterNum;
  uint32_t FileSize;
  vector<uint32_t> clusters;
  uint8_t Attribute;

  string Name;
  string Extension;
  uint16_t HighClusterNum;
  uint16_t LowClusterNum;
};

class Fat32
{
public:
  Superblock *superblock;
  FatArea *fatarea;
  DirectoryEntry *RootDir;
  DirectoryEntry *DataDir;
  ifstream* ifs;

  Fat32(ifstream *file)
  { 
    char buffer_sb[0x200] = {0};
    ifs = file; 
    ifs->read(buffer_sb, 0x200);
    superblock = new Superblock((uint8_t *)buffer_sb, 0x200);

    vector<char> buffer_fa(superblock->FatSize);
    ifs->seekg(superblock->FatOffset);
    ifs->read(buffer_fa.data(), superblock->FatSize);
    fatarea = new FatArea((uint8_t *)buffer_fa.data(), superblock->FatSize);
  }

  void build() {
    char buffer_leaf[0x20] = {0};
    ifs->seekg(superblock->DataAreaOffset + 0x80);
    ifs->read(buffer_leaf, 0x20);
    RootDir = new DirectoryEntry((uint8_t *)buffer_leaf, 0x20, *fatarea);
  }

  void export_to() {
    uint32_t filedata_addr = superblock->DataAreaOffset + (RootDir->ClusterNum - 2) * superblock->ClusterSize + 0x40;
    char buffer_data[0x20] = {0};
    ifs->seekg(filedata_addr);
    ifs->read(buffer_data, 0x20);

    DataDir = new DirectoryEntry((uint8_t *)buffer_data, 0x20, *fatarea);
    DataDir->export_to(ifs);
  }
};

int main(int argc, char *argv[])
{
  ifstream ifs("FAT32_simple.mdf"s);

  if (!ifs.good())
    cout << "error";
  Fat32 test(&ifs);
  test.build();
  test.export_to();


  return 0;
}


