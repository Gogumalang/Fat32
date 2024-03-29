#include <iostream>
#include <fstream>
#include <cstdlib>

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
    fat = new uint32_t[entry_count];
    for (int i = 0; i < entry_count; i++)
    {
      fat[i] = bb.get_uint32_le();
    }
  }

  ~FatArea(){
    delete [] fat;
  }

  int entry_count;
  uint32_t *fat;
};

class DirectoryEntry
{
public:
  DirectoryEntry(uint8_t *buffer, int size)
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
    bb.offset(0x1A);
    LowClusterNum = bb.get_uint16_le();
    ClusterNum = (HighClusterNum << 16) | LowClusterNum;
    FileSize = bb.get_uint32_le();
  }

public : 
  string FileName;
  uint32_t ClusterNum;
  uint32_t FileSize;
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
    char buffer[0x20] = {0};
    ifs->seekg(superblock->DataAreaOffset + 0x80); //D1 으로 이동 
    ifs->read(buffer, 0x20);
    RootDir = new DirectoryEntry((uint8_t *)buffer, 0x20);
  }

  vector<uint32_t> set_clusters(uint32_t cluster_no, FatArea* fatArea){

    vector<uint32_t> clusters;
    clusters.push_back(cluster_no);

    uint32_t next = fatArea->fat[cluster_no];

    while (next != 0xfffffff)
    {
      clusters.push_back(next);
      next = fatArea->fat[next];
    }

    return clusters;
  }

  void export_to() {
    uint32_t filedata_addr = superblock->DataAreaOffset + (RootDir->ClusterNum - 2) * superblock->ClusterSize + 0x40;// leaf 파일로 이동 
    char buffer_data[0x20] = {0};
    ifs->seekg(filedata_addr);
    ifs->read(buffer_data, 0x20);

    DataDir = new DirectoryEntry((uint8_t *)buffer_data, 0x20);

    ofstream ofs(DataDir->FileName, std::ios_base::binary);
    uint32_t RestOfFile = DataDir-> FileSize;
    vector<uint32_t> clusters = set_clusters(DataDir->ClusterNum,fatarea);
    for (uint32_t cluster : clusters)
    {   
      uint32_t physical_addr = 0x400000 + (cluster - 2) * 0x1000;
      ifs->seekg(physical_addr);
      char buffer[0x1000] = {0};

      uint32_t bufferSize = (RestOfFile >= 0x1000) ? 0x1000 : RestOfFile;
      ifs->read(buffer, bufferSize);
      ofs.write(buffer, bufferSize);
      RestOfFile -= 0x1000;
    }
    ofs.close();
  }

  ~Fat32() {
    delete superblock;
    delete fatarea;
    delete RootDir;
    delete DataDir;
  }
};

class Node{
  Node(string nodename, string nodetype, DirectoryEntry dentry) {
    name = nodename;
    type = nodetype;
    de = &dentry;

  }
public: 
  string name;
  string type;
  vector<Node*> children;
  Node* parent; 
  DirectoryEntry* de;

  uint32_t inode_no;
  uint8_t file_mode;
};

int main(int argc, char *argv[])
{
  ifstream ifs("FAT32_simple.mdf"s);

  if (!ifs.good()){
    cout << "failed to open fileerror";
    return EXIT_FAILURE;
  }
  Fat32 test(&ifs);
  test.build();
  test.export_to();
  ifs.close();

  return EXIT_SUCCESS;
}


