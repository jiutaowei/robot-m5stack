"""统一工具管理器"""

from typing import Dict, List, Optional, Any
from config.logger import setup_logging
from plugins_func.register import Action, ActionResponse
from .base import ToolType, ToolDefinition, ToolExecutor


class ToolManager:
    """统一工具管理器，管理所有类型的工具"""

    def __init__(self, conn):
        self.conn = conn
        self.logger = setup_logging()
        self.executors: Dict[ToolType, ToolExecutor] = {}
        self._cached_tools: Optional[Dict[str, ToolDefinition]] = None
        self._cached_function_descriptions: Optional[List[Dict[str, Any]]] = None

    def register_executor(self, tool_type: ToolType, executor: ToolExecutor):
        """注册工具执行器"""
        self.executors[tool_type] = executor
        self._invalidate_cache()
        self.logger.debug(f"注册工具执行器: {tool_type.value}")

    def _invalidate_cache(self):
        """使缓存失效"""
        self._cached_tools = None
        self._cached_function_descriptions = None

    def _excluded_names(self) -> set:
        """按配置隐藏工具（大模型看不到，同时也无法被调用）。

        data/.config.yaml:
            Intent:
              function_call:
                exclude_functions: [play_music, get_lunar]
        动机：工具清单越长，大模型"挑工具"越慢、越容易挑错，每轮 prompt 也更大/更贵。
        实测米宝 22 个工具里只有 6 个被真正调用过，其余 16 个为 0 次。

        注意：隐藏是**彻底的** —— 因为 has_tool()/execute_tool() 都走这份清单，
        所以被隐藏的工具即使大模型凭记忆点名也无法执行（这是期望行为）。
        框架内部没有对 play_music/get_lunar 这类工具做隐式调用，所以隐藏安全。
        """
        try:
            cfg = getattr(self.conn, "config", None) or {}
            func_call = (cfg.get("Intent") or {}).get("function_call") or {}
            raw = func_call.get("exclude_functions") or []
            if isinstance(raw, str):
                raw = [raw]
            if not isinstance(raw, (list, tuple)):
                return set()
            return {str(x).strip() for x in raw if str(x).strip()}
        except Exception:
            return set()

    def get_all_tools(self) -> Dict[str, ToolDefinition]:
        """获取所有工具定义"""
        if self._cached_tools is not None:
            return self._cached_tools

        excluded = self._excluded_names()
        hidden = set()
        all_tools = {}
        for tool_type, executor in self.executors.items():
            try:
                tools = executor.get_tools()
                for name, definition in tools.items():
                    if name in excluded:
                        hidden.add(name)
                        continue
                    if name in all_tools:
                        self.logger.warning(f"工具名称冲突: {name}")
                    all_tools[name] = definition
            except Exception as e:
                self.logger.error(f"获取{tool_type.value}工具时出错: {e}")

        if hidden:
            self.logger.info(
                f"按 exclude_functions 隐藏 {len(hidden)} 个工具（不发给大模型）：{sorted(hidden)}"
            )
        self._cached_tools = all_tools
        return all_tools

    def get_function_descriptions(self) -> List[Dict[str, Any]]:
        """获取所有工具的函数描述（OpenAI格式）"""
        if self._cached_function_descriptions is not None:
            return self._cached_function_descriptions

        descriptions = []
        tools = self.get_all_tools()
        for tool_definition in tools.values():
            descriptions.append(tool_definition.description)

        self._cached_function_descriptions = descriptions
        return descriptions

    def has_tool(self, tool_name: str) -> bool:
        """检查是否存在指定工具"""
        tools = self.get_all_tools()
        return tool_name in tools

    def get_tool_type(self, tool_name: str) -> Optional[ToolType]:
        """获取工具类型"""
        tools = self.get_all_tools()
        tool_def = tools.get(tool_name)
        return tool_def.tool_type if tool_def else None

    async def execute_tool(
        self, tool_name: str, arguments: Dict[str, Any]
    ) -> ActionResponse:
        """执行工具调用"""
        try:
            # 查找工具类型
            tool_type = self.get_tool_type(tool_name)
            if not tool_type:
                return ActionResponse(
                    action=Action.NOTFOUND,
                    response=f"工具 {tool_name} 不存在",
                )

            # 获取对应的执行器
            executor = self.executors.get(tool_type)
            if not executor:
                return ActionResponse(
                    action=Action.ERROR,
                    response=f"工具类型 {tool_type.value} 的执行器未注册",
                )

            # 执行工具
            self.logger.info(f"执行工具: {tool_name}，参数: {arguments}")
            result = await executor.execute(self.conn, tool_name, arguments)
            self.logger.debug(f"工具执行结果: {result}")
            return result

        except Exception as e:
            self.logger.error(f"执行工具 {tool_name} 时出错: {e}")
            return ActionResponse(action=Action.ERROR, response=str(e))

    def get_supported_tool_names(self) -> List[str]:
        """获取所有支持的工具名称"""
        tools = self.get_all_tools()
        return list(tools.keys())

    def refresh_tools(self):
        """刷新工具缓存"""
        self._invalidate_cache()
        self.logger.debug("工具缓存已刷新")

    def get_tool_statistics(self) -> Dict[str, int]:
        """获取工具统计信息"""
        stats = {}
        for tool_type, executor in self.executors.items():
            try:
                tools = executor.get_tools()
                stats[tool_type.value] = len(tools)
            except Exception as e:
                self.logger.error(f"获取{tool_type.value}工具统计时出错: {e}")
                stats[tool_type.value] = 0
        return stats
