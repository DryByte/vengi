/**
 * @file
 */

#pragma once

#include "command/CommandHandler.h"
#include "core/Var.h"

namespace video {
class Camera;
}
namespace scenegraph {
class SceneGraph;
class SceneGraphNode;
class SceneGraphNodeCamera;
}

namespace voxedit {

struct ModelNodeSettings;

class SceneGraphPanel {
private:
	core::VarPtr _animationSpeedVar;
	core::VarPtr _hideInactive;
	bool _showNodeDetails = true;
	bool _hasFocus = false;
	core::String _propertyKey;
	core::String _propertyValue;

	void detailView(scenegraph::SceneGraphNode &node);
	void recursiveAddNodes(video::Camera &camera, const scenegraph::SceneGraph &sceneGraph,
							  scenegraph::SceneGraphNode &node, command::CommandExecutionListener &listener,
							  int depth, int referencedNodeId) const;
	/**
	 * @return @c true if the property was handled with a special ui input widget - @c false if it should just be a
	 * normal text input field
	 */
	bool handleCameraProperty(scenegraph::SceneGraphNodeCamera &node, const core::String &key, const core::String &value);
public:
	bool _popupNewModelNode = false;
	bool init();
	void update(video::Camera& camera, const char *title, ModelNodeSettings* layerSettings, command::CommandExecutionListener &listener);
	bool hasFocus() const;
};

}
