#include <iostream>
#include <vector>
#include <cstring>
#include "doca_stdexec/buf.hpp"

using namespace doca_stdexec;

// Example function to demonstrate buffer usage
void demonstrate_buffer_operations() {
    // Note: In a real application, you would get doca_buf from buffer inventory
    // This is just a conceptual example showing the API usage
    
    std::cout << "DOCA Buffer C++ Binding Example\n";
    std::cout << "================================\n\n";
    
    try {
        // Example 1: Buffer with data operations
        std::cout << "1. Buffer Data Operations:\n";
        
        // In real usage, you'd get buf from doca_buf_inventory_buf_get_by_addr or similar
        // struct doca_buf *raw_buf = ...; // obtained from buffer inventory
        // Buf buffer(raw_buf);
        
        // For demonstration, we'll show what the API would look like:
        std::cout << "   - Create buffer wrapper: Buf buffer(raw_buf)\n";
        std::cout << "   - Get data length: buffer.get_data_len()\n";
        std::cout << "   - Get data pointer: buffer.get_data()\n";
        std::cout << "   - Get data as span: buffer.get_data_span()\n";
        std::cout << "   - Set data: buffer.set_data(ptr, len)\n";
        
        // Example 2: Reference counting
        std::cout << "\n2. Reference Counting:\n";
        std::cout << "   - Get refcount: buffer.get_refcount()\n";
        std::cout << "   - Increment: buffer.inc_refcount()\n";
        std::cout << "   - Decrement: buffer.dec_refcount()\n";
        
        // Example 3: List operations
        std::cout << "\n3. List Operations:\n";
        std::cout << "   - Check if in list: buffer.is_in_list()\n";
        std::cout << "   - Get list length: buffer.get_list_len()\n";
        std::cout << "   - Collect all buffers: buffer.collect_list()\n";
        
        // Example 4: Template operations
        std::cout << "\n4. Type-safe Operations:\n";
        std::cout << "   - Get typed data: buffer.get_data_as<uint32_t>()\n";
        std::cout << "   - Get typed span: buffer.get_data_span_as<uint32_t>()\n";
        std::cout << "   - Set from span: buffer.set_data(my_span)\n";
        
        std::cout << "\n5. RAII Benefits:\n";
        std::cout << "   - Automatic reference counting\n";
        std::cout << "   - Exception safety\n";
        std::cout << "   - No manual cleanup needed\n";
        
    } catch (const BufException &e) {
        std::cerr << "Buffer operation failed: " << e.what() << std::endl;
        std::cerr << "Error code: " << e.get_error_code() << std::endl;
    }
}

// Example showing buffer list traversal
void demonstrate_list_traversal() {
    std::cout << "\nBuffer List Traversal Example:\n";
    std::cout << "==============================\n";
    
    // Conceptual example of traversing a buffer list
    std::cout << "Traversing buffer list:\n";
    std::cout << R"(
    auto buffers = head_buffer.collect_list();
    for (const auto& buf : buffers) {
        std::cout << "Buffer data length: " << buf.get_data_len() << std::endl;
        auto data_span = buf.get_data_span();
        // Process buffer data...
    }
    
    // Or manual traversal:
    Buf current = head_buffer;
    while (true) {
        // Process current buffer
        std::cout << "Processing buffer..." << std::endl;
        
        Buf next;
        if (!current.get_next_in_list(next)) {
            break; // End of list
        }
        current = next;
    }
)";
}

// Example showing type-safe buffer operations
void demonstrate_type_safety() {
    std::cout << "\nType-Safe Buffer Operations:\n";
    std::cout << "============================\n";
    
    std::cout << R"(
    // Working with structured data
    struct MyData {
        uint32_t id;
        float value;
        char name[32];
    };
    
    // Get buffer data as typed span
    try {
        auto data_span = buffer.get_data_span_as<MyData>();
        for (auto& item : data_span) {
            std::cout << "ID: " << item.id << ", Value: " << item.value << std::endl;
        }
    } catch (const BufException& e) {
        // Handle alignment or size errors
        std::cerr << "Type conversion failed: " << e.what() << std::endl;
    }
    
    // Setting data from span
    std::vector<uint32_t> my_data = {1, 2, 3, 4, 5};
    std::span<uint32_t> data_span(my_data);
    buffer.set_data(data_span);
)";
}

int main() {
    demonstrate_buffer_operations();
    demonstrate_list_traversal();
    demonstrate_type_safety();
    
    std::cout << "\nNote: This example shows the API usage patterns.\n";
    std::cout << "In a real application, you would:\n";
    std::cout << "1. Create a buffer inventory\n";
    std::cout << "2. Get buffers from the inventory\n";
    std::cout << "3. Use the C++ wrapper for safe operations\n";
    std::cout << "4. The wrapper automatically manages references\n";
    
    return 0;
} 