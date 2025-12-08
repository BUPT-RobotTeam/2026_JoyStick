# 手柄数据通信协议文档（CRC32校验版）

## 版本信息
- **协议版本**: V2.0
- **发布日期**: 2025年12月7日
- **校验方式**: CRC-32

---

## 1. 数据包格式概述

数据包采用二进制格式传输，总长度为**25字节**，结构如下：

| 字段名 | 类型 | 字节数 | 偏移量 | 说明 |
|--------|------|--------|--------|------|
| head | char | 1 | 0 | 包头标识，固定为 `'+'` (0x2B) |
| id | uint32_t | 4 | 1 | 数据包序号（递增），用于判断报文先后顺序 |
| action[4] | int16_t × 4 | 8 | 5 | 摇杆数据数组 |
| button | uint32_t | 4 | 13 | 按钮状态位图 |
| reserve | uint32_t | 4 | 17 | 保留字段（当前为0） |
| crc32 | uint32_t | 4 | 21 | CRC-32校验值 |
| tail | char | 1 | 24 | 包尾标识，固定为 `'*'` (0x2A) |

**注意**: 结构体使用 `__attribute__((packed))` 属性，无内存对齐，按字节紧密排列。

---

## 2. 字段详细说明

### 2.1 head（包头）
- **类型**: `char`
- **值**: 固定为 `'+'` (ASCII码: 0x2B)
- **用途**: 用于识别数据包起始位置，便于同步和解析

### 2.2 id（数据包序号）
- **类型**: `uint32_t`（4字节，小端序）
- **范围**: 0 ~ 4294967295
- **行为**: 每发送一个数据包，id递增1，溢出后从0重新开始
- **用途**: 
  - 判断数据包的先后顺序
  - 检测数据包丢失（id不连续）
  - 检测数据包重复

### 2.3 action[4]（摇杆数据）
- **类型**: `int16_t` 数组，共4个元素（8字节）
- **数据顺序**:
  - `action[0]`: 左摇杆Y轴 (left_Y)
  - `action[1]`: 左摇杆X轴 (left_X)
  - `action[2]`: 右摇杆Y轴 (right_Y)
  - `action[3]`: 右摇杆X轴 (right_X)
- **数值范围**: -100 ~ 100
  - **0**: 摇杆居中位置
  - **正值**: Y轴向上/X轴向右
  - **负值**: Y轴向下/X轴向左
- **死区**: 绝对值小于7的会被设为0，避免摇杆抖动

### 2.4 button（按钮状态）
- **类型**: `uint32_t`（4字节）
- **格式**: 位图（bitmap），每一位代表一个按钮的状态
- **按钮映射**:

| 位 | 按钮编号 | 状态 |
|----|----------|------|
| bit 0 | 按钮0 | 0=释放，1=按下 |
| bit 1 | 按钮1 | 0=释放，1=按下 |
| bit 2 | 按钮2 | 0=释放，1=按下 |
| bit 3 | 按钮3 | 0=释放，1=按下 |
| bit 4 | 按钮4 | 0=释放，1=按下 |
| bit 5 | 按钮5 | 0=释放，1=按下 |
| bit 6 | 按钮6 | 0=释放，1=按下 |
| bit 7 | 按钮7 | 0=释放，1=按下 |
| bit 8~31 | 未使用 | 保留为0 |

**示例**:
- `button = 0x00000001` (二进制: 00000001) → 按钮0按下
- `button = 0x00000005` (二进制: 00000101) → 按钮0和按钮2按下
- `button = 0x000000FF` (二进制: 11111111) → 所有8个按钮都按下

### 2.5 reserve（保留字段）
- **类型**: `uint32_t`（4字节）
- **当前值**: 0
- **用途**: 为未来功能扩展预留，接收方可忽略此字段

### 2.6 crc32（CRC校验值）
- **类型**: `uint32_t`（4字节）
- **算法**: CRC-32（标准多项式 0xEDB88320）
- **校验范围**: 从 `id` 字段到 `button` 字段（共16字节）
  - 包括: `id`(4字节) + `action[4]`(8字节) + `button`(4字节)
- **用途**: 验证数据传输的完整性和正确性
- **计算方法**:
  ```
  初始值: 0xFFFFFFFF
  多项式: 0x04C11DB7
  最终结果: 按位取反
  ```

**重要**: 
- **不校验** `head` 字段
- **不校验** `reserve` 字段
- **不校验** `tail` 字段
- **不校验** `crc32` 字段本身

### 2.7 tail（包尾）
- **类型**: `char`
- **值**: 固定为 `'*'` (ASCII码: 0x2A)
- **用途**: 标识数据包结束，用于数据包完整性检查

---

## 3. 数据解析流程

### 3.1 接收端解析步骤

```
1. 接收数据流，查找包头 '+' (0x2B)
2. 从包头开始，接收完整的25字节数据包
3. 检查包尾是否为 '*' (0x2A)
   - 如果不是，丢弃该包，回到步骤1
4. 提取 id、action[4]、button 字段（共16字节）
5. 计算这16字节的CRC32值
6. 将计算结果与数据包中的 crc32 字段比对
   - 如果相同，校验通过，数据有效
   - 如果不同，校验失败，丢弃该包，回到步骤1
7. 解析有效数据：
   - 根据 id 判断数据包顺序
   - 提取 action[4] 获取摇杆数据
   - 解析 button 获取按钮状态
```

### 3.2 C语言解析示例

```c
#include <stdint.h>
#include <stdbool.h>

// 数据包结构体定义
typedef struct __attribute__((packed)) {
    char head;              // 包头 '+'
    uint32_t id;            // 数据包序号
    int16_t action[4];      // 摇杆数据
    uint32_t button;        // 按钮状态
    uint32_t reserve;       // 保留字段
    uint32_t crc32;         // CRC32校验值
    char tail;              // 包尾 '*'
} CommPacket_t;

// CRC32计算函数
uint32_t Calculate_CRC32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    const uint32_t polynomial = 0xEDB88320;
    
    for (uint32_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ polynomial;
            else
                crc = crc >> 1;
        }
    }
    
    return ~crc;
}

// 校验数据包
bool Verify_Packet(const CommPacket_t *packet)
{
    // 1. 检查包头包尾
    if (packet->head != '+' || packet->tail != '*')
        return false;
    
    // 2. 计算CRC32（从id到button，共16字节）
    const uint8_t *crc_data = (const uint8_t*)&packet->id;
    uint32_t crc_length = sizeof(packet->id) + 
                          sizeof(packet->action) + 
                          sizeof(packet->button);
    uint32_t calculated_crc = Calculate_CRC32(crc_data, crc_length);
    
    // 3. 比对CRC32
    if (calculated_crc != packet->crc32)
        return false;
    
    return true;
}

// 解析按钮状态
bool Get_Button_State(uint32_t button_bitmap, uint8_t button_index)
{
    return (button_bitmap & (1 << button_index)) != 0;
}

// 使用示例
void Process_Packet(const uint8_t *raw_data)
{
    CommPacket_t *packet = (CommPacket_t*)raw_data;
    
    // 校验数据包
    if (!Verify_Packet(packet))
    {
        // 数据包损坏，丢弃
        return;
    }
    
    // 提取数据
    uint32_t packet_id = packet->id;
    int16_t left_y = packet->action[0];
    int16_t left_x = packet->action[1];
    int16_t right_y = packet->action[2];
    int16_t right_x = packet->action[3];
    
    // 检查各个按钮状态
    for (uint8_t i = 0; i < 8; i++)
    {
        bool is_pressed = Get_Button_State(packet->button, i);
        // 处理按钮状态...
    }
}
```

### 3.3 Python解析示例

```python
import struct

class GamepadPacket:
    PACKET_SIZE = 25
    HEAD = ord('+')
    TAIL = ord('*')
    
    def __init__(self):
        self.head = 0
        self.id = 0
        self.action = [0, 0, 0, 0]
        self.button = 0
        self.reserve = 0
        self.crc32 = 0
        self.tail = 0
    
    @staticmethod
    def calculate_crc32(data):
        """计算CRC-32校验值"""
        crc = 0xFFFFFFFF
        polynomial = 0xEDB88320
        
        for byte in data:
            crc ^= byte
            for _ in range(8):
                if crc & 1:
                    crc = (crc >> 1) ^ polynomial
                else:
                    crc >>= 1
        
        return (~crc) & 0xFFFFFFFF
    
    def parse(self, data):
        """解析二进制数据包"""
        if len(data) != self.PACKET_SIZE:
            return False
        
        # 解包数据（小端序）
        # 格式: c=char, I=uint32, h=int16
        unpacked = struct.unpack('<cI4hIIIc', data)
        
        self.head = ord(unpacked[0])
        self.id = unpacked[1]
        self.action = list(unpacked[2:6])
        self.button = unpacked[6]
        self.reserve = unpacked[7]
        self.crc32 = unpacked[8]
        self.tail = ord(unpacked[9])
        
        # 校验包头包尾
        if self.head != self.HEAD or self.tail != self.TAIL:
            return False
        
        # 校验CRC32（从id到button，共16字节）
        crc_data = data[1:17]  # 偏移1，长度16
        calculated_crc = self.calculate_crc32(crc_data)
        
        if calculated_crc != self.crc32:
            return False
        
        return True
    
    def get_button_state(self, button_index):
        """获取指定按钮的状态"""
        return bool(self.button & (1 << button_index))
    
    def __str__(self):
        buttons = [self.get_button_state(i) for i in range(8)]
        return (f"Packet ID: {self.id}\n"
                f"Left Stick: ({self.action[0]}, {self.action[1]})\n"
                f"Right Stick: ({self.action[2]}, {self.action[3]})\n"
                f"Buttons: {buttons}")

# 使用示例
def process_data(serial_port):
    """从串口读取并处理数据包"""
    packet = GamepadPacket()
    
    while True:
        # 查找包头
        byte = serial_port.read(1)
        if byte[0] != GamepadPacket.HEAD:
            continue
        
        # 读取剩余24字节
        remaining = serial_port.read(24)
        raw_data = byte + remaining
        
        # 解析数据包
        if packet.parse(raw_data):
            print(packet)
            # 进一步处理数据...
        else:
            print("Invalid packet received")
```

---

## 4. 数据传输特性

### 4.1 传输频率
- **发送频率**: 50 Hz（每20毫秒发送一次）
- **单包大小**: 25字节
- **数据率**: 1250字节/秒 (约 10 kbps)

### 4.2 数据完整性保证
1. **包头包尾**: 双重标识，确保数据包边界正确
2. **CRC32校验**: 检测传输错误和数据损坏
3. **序号机制**: 检测丢包和乱序

### 4.3 容错处理建议
- **CRC校验失败**: 丢弃当前包，等待下一个有效包
- **id不连续**: 记录丢包事件，使用最新有效数据
- **超时处理**: 若超过100ms未收到新包，应考虑连接中断

---

## 5. 数据包示例

### 5.1 示例1：摇杆居中，无按钮按下

**十六进制表示**:
```
2B 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 XX XX XX XX 2A
```

**字段解析**:
- `2B`: head = '+'
- `00 00 00 00`: id = 0
- `00 00 00 00 00 00 00 00`: action = [0, 0, 0, 0]
- `00 00 00 00`: button = 0
- `00 00 00 00`: reserve = 0
- `XX XX XX XX`: crc32（根据前面数据计算）
- `2A`: tail = '*'

### 5.2 示例2：左摇杆向上推满，按钮0按下

**字段值**:
- id = 100 (0x64)
- action[0] = 100 (0x0064)
- action[1] = 0 (0x0000)
- action[2] = 0 (0x0000)
- action[3] = 0 (0x0000)
- button = 1 (0x00000001)

**十六进制表示** (小端序):
```
2B 64 00 00 00 64 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00 XX XX XX XX 2A
```

---

## 6. 常见问题解答

### Q1: 为什么要使用CRC32而不是简单的校验和？
A: CRC32能够检测更多类型的错误，包括突发错误、位移错误等，可靠性更高。

### Q2: 如果CRC校验失败应该怎么处理？
A: 应丢弃该数据包，不要使用其中的数据。等待下一个有效的数据包到达。

### Q3: id溢出后会影响通信吗？
A: 不会。id溢出后会从0重新开始，接收方应当能够处理这种情况。

### Q4: reserve字段将来会用于什么？
A: 保留字段可用于未来的功能扩展，如增加传感器数据、电池电量等信息。

### Q5: 为什么action和button字段要用CRC32校验，而reserve不用？
A: action和button是关键控制数据，必须保证准确性。reserve字段当前未使用，即使损坏也不影响功能。

---

## 7. 附录

### 7.1 结构体定义（C语言）

```c
typedef struct __attribute__((packed)) {
    char head;              // 包头 '+'
    uint32_t id;            // 数据包序号
    int16_t action[4];      // 摇杆数据 [left_Y, left_X, right_Y, right_X]
    uint32_t button;        // 按钮状态位图
    uint32_t reserve;       // 保留字段
    uint32_t crc32;         // CRC32校验值
    char tail;              // 包尾 '*'
} CommPacket_t;
```

### 7.2 字节序
- 所有多字节字段（uint32_t, int16_t）均采用**小端序**（Little-Endian）
- 这与STM32系列MCU的默认字节序一致

### 7.3 通信接口
- **接口**: UART3
- **波特率**: (请根据实际配置填写)
- **数据位**: 8
- **停止位**: 1
- **校验位**: 无

---

## 8. 更新日志

| 版本 | 日期 | 更新内容 |
|------|------|----------|
| V2.0 | 2025-12-07 | 采用新的二进制数据包格式，增加CRC32校验 |
| V1.0 | (之前) | 使用文本格式传输数据 |

---

**文档结束**

如有疑问，请联系开发人员。

