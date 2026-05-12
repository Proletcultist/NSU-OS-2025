const std = @import("std");
const Mutex = std.Thread.Mutex;
const Allocator = std.mem.Allocator;

// Node of linked list
const Node = struct {
    value: []const u8 = undefined,
    next: ?*Node = null,
};

const StringList = struct {
    first: *Node,
    last: *Node,
    len: usize = 0,
    alloc: Allocator,

    fn init(alloc: Allocator) !StringList {
        const sentinel: *Node = try alloc.create(Node);
        sentinel.* = .{};
        return .{.first = sentinel, .last = sentinel, .alloc = alloc};
    }

    fn prepend(self: *StringList, val: []const u8) !void {
        const new: *Node = try self.alloc.create(Node);
        new.* = .{.value = val};
        
        self.last.next = new;
        self.last = new;

        self.len += 1;
    }

    fn print(self: *StringList, w: *std.io.Writer) !void {
        var current = self.first.next;
        while (current != null) : (current = current.?.next) {
            try w.print("{s}\n", .{current.?.value});
        }
    }

    fn deinit(self: *StringList) void {
        var current: ?*Node = self.first;
        while (current != null) {
            const tmp = current.?.next;
            self.alloc.destroy(current.?);
            current = tmp;
        }
    }
};

// String list
var list: StringList = undefined;
var list_mtx: Mutex = .{};
  
fn sort_list(li: *StringList) void {
    for (0..li.len) |i| {
        var swapped = false;

        var current = li.first;
        for (0..li.len - i - 1) |_| {
            const this = current.next.?;
            const this_next = this.next.?;

            if (std.mem.order(u8, this.value, this_next.value) == std.math.Order.gt) {
                this.next = this_next.next;
                this_next.next = this;
                current.next = this_next;
                if (this.next == null) {
                    li.last = this;
                }

                swapped = true;
            }

            current = current.next.?;
        }

        if (!swapped) {
            break;
        }
    }
}

fn sorter_routine() noreturn {
    while (true) {
        list_mtx.lock();
        sort_list(&list);
        list_mtx.unlock();

        std.Thread.sleep(5000000000);
    }
}

pub fn main() !void {
    // Init buffered stdin reader
    var stdin_buf: [1024]u8 = undefined;
    var stdin_reader = std.fs.File.stdin().reader(&stdin_buf);
    const stdin = &stdin_reader.interface;

    // Init buffered stdout writer
    var stdout_buf: [1024]u8 = undefined;
    var stdout_writer = std.fs.File.stdout().writer(&stdout_buf);
    const stdout = &stdout_writer.interface;

    // Init allocator
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();
    const allocator = arena.allocator();

    // Init string list
    list = try StringList.init(allocator);
    defer list.deinit();

    // Spawn sorter thread
    _ = try std.Thread.spawn(.{}, sorter_routine, .{});

    var input_buffer = std.io.Writer.Allocating.init(allocator);
    defer input_buffer.deinit();

    while (true) {
        if (stdin.streamDelimiterLimit(&input_buffer.writer, '\n', .unlimited)) |readen| {
            list_mtx.lock();
            defer list_mtx.unlock();
            if (readen == 0 and stdin.seek != stdin.end) {
                try list.print(stdout);
                try stdout.flush();
            }
            else {
                try list.prepend(try input_buffer.toOwnedSlice());
            }

            if (stdin.seek == stdin.end) break
            else stdin.toss(1);
        }
        else |err| switch (err) {
            error.StreamTooLong => unreachable,
            else => |e| return e
        }
    }

    var current = list.first.next;
    while (current != null) : (current = current.?.next) {
        input_buffer.allocator.free(current.?.value);
    }
}
