// Copyright (c) 2009 Satoshi Nakamoto
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

class CUser;
class CReview;
class CProduct;

static const unsigned int nFlowthroughRate = 2;

bool AdvertInsert(const CProduct &product);
void AdvertErase(const CProduct &product);
bool AddAtomsAndPropagate(uint256 hashUserStart,
                          const vector<unsigned short> &vAtoms, bool fOrigin);

// 表示 “用户”，维护与 “原子（atoms）”
// 相关的三个列表（vAtomsIn输入原子、
// vAtomsNew新原子、vAtomsOut输出原子）和用户链接vLinksOut。
// 提供原子添加（AddAtom）和原子计数（GetAtomCount）功能。
class CUser {
public:
  vector<unsigned short> vAtomsIn;  // 收到的原子数量
  vector<unsigned short> vAtomsNew; // 新获得的原子（待处理）
  vector<unsigned short> vAtomsOut; // 发出的原子（用于传播）
  vector<uint256> vLinksOut;        // 用户关联的对象（用于原子传播）

  CUser() {}

  IMPLEMENT_SERIALIZE(if (!(nType & SER_GETHASH)) READWRITE(nVersion);
                      READWRITE(vAtomsIn); READWRITE(vAtomsNew);
                      READWRITE(vAtomsOut); READWRITE(vLinksOut);)

  void SetNull() {
    vAtomsIn.clear();
    vAtomsNew.clear();
    vAtomsOut.clear();
    vLinksOut.clear();
  }

  uint256 GetHash() const { return SerializeHash(*this); }

  // 计算用户的原子总数（活跃度/信誉的量化值）
  // 活跃度 = 收到的原子数量 + 新获得的原子数量
  int GetAtomCount() const { return (vAtomsIn.size() + vAtomsNew.size()); }
  // 添加原子的方法
  void AddAtom(unsigned short nAtom, bool fOrigin);
};

// 表示
// “评论”，包含评论目标哈希hashTo、
// 评论内容mapValue、
// 评论者公钥vchPubKeyFrom及签名vchSig。
// 支持评论的接受逻辑（AcceptReview），包括签名验证、
// 写入数据库、建立用户链接及原子传播
class CReview {
public:
  int nVersion;
  uint256 hashTo;
  map<string, string> mapValue;
  vector<unsigned char> vchPubKeyFrom;
  vector<unsigned char> vchSig;

  // memory only
  unsigned int nTime;
  int nAtoms;

  CReview() {
    nVersion = 1;
    hashTo = 0;
    nTime = 0;
    nAtoms = 0;
  }

  IMPLEMENT_SERIALIZE(READWRITE(this->nVersion); nVersion = this->nVersion;
                      if (!(nType & SER_DISK)) READWRITE(hashTo);
                      READWRITE(mapValue); READWRITE(vchPubKeyFrom);
                      if (!(nType & SER_GETHASH)) READWRITE(vchSig);)

  uint256 GetHash() const { return SerializeHash(*this); }
  uint256 GetSigHash() const {
    return SerializeHash(*this, SER_GETHASH | SER_SKIPSIG);
  }
  uint256 GetUserHash() const {
    return Hash(vchPubKeyFrom.begin(), vchPubKeyFrom.end());
  }

  bool AcceptReview();
};

//// later figure out how these are persisted
// 表示
// “产品”，包含产品基本信息（地址、键值对属性mapValue）、
// 详细信息mapDetails、
// 订单表单vOrderForm、
// 发布者公钥vchPubKeyFrom及签名vchSig。
// 支持签名验证（CheckSignature）和产品合法性检查（CheckProduct）。
class CProduct {
public:
  int nVersion;
  CAddress addr;
  map<string, string> mapValue;
  map<string, string> mapDetails;
  vector<pair<string, string>> vOrderForm;
  unsigned int nSequence;
  vector<unsigned char> vchPubKeyFrom;
  vector<unsigned char> vchSig;

  // disk only
  int nAtoms;

  // memory only
  set<unsigned int> setSources;

  CProduct() {
    nVersion = 1;
    nAtoms = 0;
    nSequence = 0;
  }

  IMPLEMENT_SERIALIZE(READWRITE(this->nVersion); nVersion = this->nVersion;
                      READWRITE(addr); READWRITE(mapValue);
                      if (!(nType & SER_GETHASH)) {
                        READWRITE(mapDetails);
                        READWRITE(vOrderForm);
                        READWRITE(nSequence);
                      } READWRITE(vchPubKeyFrom);
                      if (!(nType & SER_GETHASH)) READWRITE(vchSig);
                      if (nType & SER_DISK) READWRITE(nAtoms);)

  uint256 GetHash() const { return SerializeHash(*this); }
  uint256 GetSigHash() const {
    return SerializeHash(*this, SER_GETHASH | SER_SKIPSIG);
  }
  uint256 GetUserHash() const {
    return Hash(vchPubKeyFrom.begin(), vchPubKeyFrom.end());
  }

  bool CheckSignature();
  bool CheckProduct();
};

extern map<uint256, CProduct> mapProducts;
extern CCriticalSection cs_mapProducts;
extern map<uint256, CProduct> mapMyProducts;
