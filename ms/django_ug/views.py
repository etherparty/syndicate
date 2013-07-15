'''
We're going to assume you need to be logged in already and have a valid session
for this to work.
'''
import logging

from django_lib.auth import authenticate
from django_lib import gatewayforms
from django_lib import forms as libforms
from django.http import HttpResponse, HttpResponseRedirect
from django.template import Context, loader, RequestContext
from django.views.decorators.csrf import csrf_exempt, csrf_protect

from storage.storagetypes import transactional
import storage.storage as db
from MS.volume import Volume
from MS.user import SyndicateUser as User
from MS.gateway import UserGateway as UG

@authenticate
def allgateways(request):
    session = request.session
    username = session['login_email']
    user = db.read_user(username)

    try:
        qry = db.list_user_gateways()
    except:
        qry = []
    gateways = []
    for g in qry:
        gateways.append(g)
    vols = []
    for g in gateways:
        attrs = {"Volume.volume_id":"== " + str(g.volume_id)}
        vols.append(db.get_volume(**attrs))
    owners = []
    for v in vols:
        volume_owner = v.owner_id
        attrs = {"SyndicateUser.owner_id":"== " + str(volume_owner)}
        owners.append(db.read_user(**attrs))
    gateway_vols_owners = zip(gateways, vols, owners)
    t = loader.get_template('gateway_templates/allusergateways.html')
    c = RequestContext(request, {'username':username, 'gateway_vols_owners':gateway_vols_owners})
    return HttpResponse(t.render(c))

@authenticate
def mygateways(request):
    session = request.session
    username = session['login_email']
    user = db.read_user(username)

    # should change this
    try:
        attrs = {"UserGateway.owner_id":"== " + str(user.owner_id)}
        qry = db.list_user_gateways(**attrs)
    except:
        qry = []
    gateways = []
    for g in qry:
        gateways.append(g)
    vols = []
    for g in gateways:
        attrs = {"Volume.volume_id":"== " + str(g.volume_id)}
        vols.append(db.get_volume(**attrs))
    gateway_vols = zip(gateways, vols)
    t = loader.get_template('gateway_templates/myusergateways.html')
    c = RequestContext(request, {'username':username, 'gateway_vols':gateway_vols})
    return HttpResponse(t.render(c))


@csrf_exempt
@authenticate
def create(request):

    session = request.session
    username = session['login_email']
    user = db.read_user(username)
    message = ""

    def give_create_form(username, message):
        form = gatewayforms.CreateUG()
        t = loader.get_template('gateway_templates/create_user_gateway.html')
        c = RequestContext(request, {'username':username,'form':form, 'message':message})
        return HttpResponse(t.render(c))

    if request.POST:
        # Validate input forms
        form = gatewayforms.CreateUG(request.POST)
        if form.is_valid():
            vol = db.read_volume(form.cleaned_data['volume_name'])
            if not vol:
                message = "No volume %s exists." % form.cleaned_data['volume_name']
                return give_create_form(username, message)
            if (vol.volume_id not in user.volumes_r) and (vol.volume_id not in user.volumes_rw):
                message = "Must have read rights to volume %s to create UG for it." % volume_name
                return give_create_form(username, message)
            try:
                kwargs = {}
                kwargs['read_write'] = form.cleaned_data['read_write']
                kwargs['ms_username'] = form.cleaned_data['g_name']
                kwargs['port'] = form.cleaned_data['port']
                kwargs['host'] = form.cleaned_data['host']
                kwargs['ms_password'] = form.cleaned_data['g_password']
                new_ug = db.create_user_gateway(user, vol, **kwargs)
            except Exception as E:
                message = "UG creation error: %s" % E
                return give_create_form(username, message)

            session['new_change'] = "Your new gateway is ready."
            session['next_url'] = '/syn/UG/mygateways'
            session['next_message'] = "Click here to see your gateways."
            return HttpResponseRedirect('/syn/thanks/')
        else:
            # Prep returned form values (so they don't have to re-enter stuff)

            if 'g_name' in form.errors:
                oldname = ""
            else:
                oldname = request.POST['g_name']
            if 'volume_name' in form.errors:
                oldvolume_name = ""
            else:
                oldvolume_name = request.POST['volume_name']
            if 'host' in form.errors:
                oldhost = ""
            else:
                oldhost = request.POST['host']
            if 'port' in form.errors:
                oldport = ""
            else:
                oldport = request.POST['port']

            # Prep error message
            message = "Invalid form entry: "

            for k, v in form.errors.items():
                message = message + "\"" + k + "\"" + " -> " 
                for m in v:
                    message = message + m + " "

            # Give then the form again
            form = gatewayforms.CreateUG(initial={'g_name': oldname,
                                       'volume_name': oldvolume_name,
                                       'host': oldhost,
                                       'port': oldport,
                                       })
            t = loader.get_template('gateway_templates/create_user_gateway.html')
            c = RequestContext(request, {'username':username,'form':form, 'message':message})
            return HttpResponse(t.render(c))

    else:
        # Not a POST, give them blank form
        return give_create_form(username, message)

@csrf_exempt
@authenticate
def delete(request, g_name):

    def give_delete_form(username, g_name, message):
        form = gatewayforms.DeleteGateway()
        t = loader.get_template('gateway_templates/delete_user_gateway.html')
        c = RequestContext(request, {'username':username, 'g_name':g_name, 'form':form, 'message':message})
        return HttpResponse(t.render(c))

    session = request.session
    username = session['login_email']
    user = db.read_user(username)
    message = ""

    ug = db.read_user_gateway(g_name)
    if not ug:
        t = loader.get_template('gateway_templates/delete_user_gateway_failure.html')
        c = RequestContext(request, {'username':username, 'g_name':g_name})
        return HttpResponse(t.render(c))

    if ug.owner_id != user.owner_id:
                t = loader.get_template('gateway_templates/delete_user_gateway_failure.html')
                c = RequestContext(request, {'username':username, 'g_name':g_name})
                return HttpResponse(t.render(c))

    if request.POST:
        # Validate input forms
        form = gatewayforms.DeleteGateway(request.POST)
        if form.is_valid():
            if not UG.authenticate(ug, form.cleaned_data['g_password']):
                message = "Incorrect User Gateway password"
                return give_delete_form(username, g_name, message)
            if not form.cleaned_data['confirm_delete']:
                message = "You must tick the delete confirmation box."
                return give_delete_form(user, g_name, message)
            
            db.delete_user_gateway(g_name)
            session['new_change'] = "Your gateway has been deleted."
            session['next_url'] = '/syn/UG/mygateways'
            session['next_message'] = "Click here to see your gateways."
            return HttpResponseRedirect('/syn/thanks/')

        # invalid forms
        else:  
            # Prep error message
            message = "Invalid form entry: "

            for k, v in form.errors.items():
                message = message + "\"" + k + "\"" + " -> " 
                for m in v:
                    message = message + m + " "

            return give_delete_form(username, g_name, message)
    else:
        # Not a POST, give them blank form
        return give_delete_form(username, g_name, message)

@csrf_exempt
@authenticate
def urlcreate(request, volume_name, g_name, g_password, host, port, read_write):
    session = request.session
    username = session['login_email']
    user = db.read_user(username)

    kwargs = {}

    kwargs['port'] = int(port)
    kwargs['host'] = host
    kwargs['ms_username'] = g_name
    kwargs['ms_password'] = g_password
    kwargs['read_write'] = read_write
    vol = db.read_volume(volume_name)
    if not vol:
        return HttpResponse("No volume %s exists." % volume_name)
    if (vol.volume_id not in user.volumes_r) and (vol.volume_id not in user.volumes_rw):
        return HttpResponse("Must have read rights to volume %s to create UG for it." % volume_name)

    try:
        new_ug = db.create_user_gateway(user, vol, **kwargs)
    except Exception as E:
        return HttpResponse("UG creation error: %s" % E)

    return HttpResponse("UG succesfully created: " + str(new_ug))

@csrf_exempt
@authenticate
def urldelete(request, g_name, g_password):
    session = request.session
    username = session['login_email']
    user = db.read_user(username)

    ug = db.read_user_gateway(g_name)
    if not ug:
        return HttpResponse("UG %s does not exist." % g_name)
    if ug.owner_id != user.owner_id:
        return HttpResponse("You must own this UG to delete it.")
    if not UG.authenticate(ug, g_password):
        return HttpResponse("Incorrect UG password.")
    db.delete_user_gateway(g_name)
    return HttpResponse("Gateway succesfully deleted.")